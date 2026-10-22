#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>

#include "UnitTest.h"
#include "WeatherProvider.h"
#include "mocks/MockTelemetryBridge.h"

// Real responses saved 22 Oct 2026 (aviationweather.gov /api/data/metar,
// api.open-meteo.com current=...). W4: the current METAR JSON format.
static const char kMetarHAAB[] = R"JSON([{"icaoId":"HAAB","receiptTime":"2026-10-22T08:55:58.741Z","obsTime":1792659600,"reportTime":"2026-10-22T09:00:00.000Z","temp":24,"dewp":5,"wdir":60,"wspd":5,"visib":"6+","altim":1028,"qcField":16,"metarType":"METAR","rawOb":"METAR HAAB 220900 06005KT CAVOK 24/05 Q1028 NOSIG","lat":8.978,"lon":38.799,"elev":2319,"name":"Addis Ababa/Bole Intl, AA, ET","cover":"CAVOK","clouds":[],"fltCat":"VFR"}])JSON";
static const char kMetarKBOS[] = R"JSON([{"icaoId":"KBOS","receiptTime":"2026-10-22T08:58:13.954Z","obsTime":1792659240,"reportTime":"2026-10-22T09:00:00.000Z","temp":8.3,"dewp":2.8,"wdir":340,"wspd":10,"wgst":18,"visib":"10+","altim":1014.6,"slp":1014.5,"qcField":4,"presTend":-1.8,"metarType":"METAR","rawOb":"METAR KBOS 220854Z 34010G18KT 10SM FEW180 BKN250 08/03 A2996 RMK AO2 SLP145 T00830028 56018","lat":42.3606,"lon":-71.0097,"elev":4,"name":"Boston/Logan Intl, MA, US","cover":"BKN","clouds":[{"cover":"FEW","base":18000},{"cover":"BKN","base":25000}],"fltCat":"VFR"}])JSON";
static const char kMetarENGM[] = R"JSON([{"icaoId":"ENGM","receiptTime":"2026-10-22T08:56:15.828Z","obsTime":1792659000,"reportTime":"2026-10-22T09:00:00.000Z","temp":10,"dewp":9,"wdir":110,"wspd":9,"visib":3.73,"altim":1006,"qcField":0,"wxString":"-RA","metarType":"METAR","rawOb":"METAR ENGM 220850Z 11009KT 6000 -RA BKN005 10/09 Q1006 TEMPO BKN008","lat":60.201,"lon":11.08,"elev":204,"name":"Oslo/Gardermoen Arpt, AK, NO","cover":"BKN","clouds":[{"cover":"BKN","base":500}],"fltCat":"IFR"}])JSON";
static const char kMetarRJTT[] = R"JSON([{"icaoId":"RJTT","receiptTime":"2026-10-22T08:40:14.966Z","obsTime":1792657800,"reportTime":"2026-10-22T08:30:00.000Z","temp":21,"dewp":6,"wdir":"VRB","wspd":5,"visib":"6+","altim":1021,"qcField":0,"metarType":"METAR","rawOb":"METAR RJTT 220830Z VRB05KT 9999 FEW030 BKN070 21/06 Q1021 BECMG TL0900 02010KT","lat":35.553,"lon":139.781,"elev":5,"name":"Tokyo/Haneda Intl, 13, JP","cover":"BKN","clouds":[{"cover":"FEW","base":3000},{"cover":"BKN","base":7000}],"fltCat":"VFR"}])JSON";
static const char kOpenMeteoAddis[] = R"JSON({"latitude":9.033392,"longitude":38.771626,"generationtime_ms":0.21159648895263672,"utc_offset_seconds":10800,"timezone":"Africa/Addis_Ababa","timezone_abbreviation":"GMT+3","elevation":2444.0,"current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C","relative_humidity_2m":"%","weather_code":"wmo code","wind_speed_10m":"m/s","wind_direction_10m":"°","wind_gusts_10m":"m/s","visibility":"m"},"current":{"time":"2026-10-22T12:00","interval":900,"temperature_2m":23.0,"relative_humidity_2m":32,"weather_code":2,"wind_speed_10m":3.18,"wind_direction_10m":123,"wind_gusts_10m":9.30,"visibility":37500.00}})JSON";

class WeatherProviderTest : public UnitTest {
    Q_OBJECT

    WeatherProvider *_wp = nullptr;

private slots:
    void init() override {
        UnitTest::init();
        if (!WeatherProvider::instance()) {
            _wp = new WeatherProvider();
        }
    }
    void cleanup() override {
        if (_wp) {
            WeatherProvider::s_instance = nullptr;
            delete _wp;
            _wp = nullptr;
        }
        UnitTest::cleanup();
    }

    void testSingletonInstance();
    void testInitialState();
    void testMetarParsing();
    void testFlightCategoryDescription();
    void testTafParsing();
    void testIcaoLookup();
    void testRealMetarFormat();
    void testForecastNeverMakesMetarFresh();
    void testForecastDoesNotOverwriteFreshMetar();
    void testMissingFieldsAreUnknown();
};

void WeatherProviderTest::testSingletonInstance()
{
    WeatherProvider *wp = WeatherProvider::instance();
    QVERIFY(wp != nullptr);
    QCOMPARE(wp, WeatherProvider::instance());
}

void WeatherProviderTest::testInitialState()
{
    WeatherProvider *wp = WeatherProvider::instance();
    QVERIFY(!wp->loading());
    QCOMPARE(wp->lastError(), QString());
    QVERIFY(!wp->metarFresh());
    // W6: nothing fetched yet = unknown, not a real-looking 0.
    QVERIFY(qIsNaN(wp->visibilityKm()));
    QVERIFY(qIsNaN(wp->temperature()));
    QVERIFY(qIsNaN(wp->windSpeed()));
    QCOMPARE(wp->ceilingFt(), -1);
    QVERIFY(wp->weatherSource().isEmpty());
    QVERIFY(!wp->windOk());
}

void WeatherProviderTest::testMetarParsing()
{
    WeatherProvider *wp = WeatherProvider::instance();

    QJsonObject metar;
    metar[QStringLiteral("rawOb")] = QStringLiteral("KLAX 012053Z 26012KT 10SM FEW040 20/15 A2992");
    metar[QStringLiteral("obsTime")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    metar[QStringLiteral("temp")] = 20.0;
    metar[QStringLiteral("dewp")] = 15.0;
    metar[QStringLiteral("wspd")] = 12.0;
    metar[QStringLiteral("wdir")] = 260.0;
    metar[QStringLiteral("visib")] = 10.0;
    metar[QStringLiteral("flightCategory")] = QStringLiteral("VFR");

    QJsonObject ceiling;
    ceiling[QStringLiteral("base_feet_agl")] = 4000;
    metar[QStringLiteral("ceiling")] = ceiling;

    QJsonArray arr;
    arr.append(metar);
    QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);

    wp->handleMetarJson(data);

    QVERIFY(qAbs(wp->windSpeed() - 6.17) < 0.01);
    QVERIFY(qAbs(wp->windDirection() - 260.0) < 0.01);
    QVERIFY(qAbs(wp->visibilityKm() - 16.09) < 0.01);
    QCOMPARE(wp->ceilingFt(), 4000);
    QVERIFY(qAbs(wp->temperature() - 20.0) < 0.1);
    QVERIFY(wp->metarString().contains(QStringLiteral("KLAX")));
    QVERIFY(wp->humidity() > 50.0);
    QVERIFY(wp->humidity() < 100.0);
    QVERIFY(wp->metarTimestamp().isValid());
    QVERIFY(wp->metarFresh());

    // Verify parsed display lines
    QStringList parsed = wp->metarParsed();
    QVERIFY(parsed.size() >= 3);
    QVERIFY(parsed[0].contains(QStringLiteral("Wind")));
    QVERIFY(parsed[1].contains(QStringLiteral("Vis")));
    QVERIFY(parsed[2].contains(QStringLiteral("Ceiling")));
}

// W5: a METAR flight category is described by name; LIFR must never read as
// "Thunderstorm" (it used to be mapped to WMO code 95).
void WeatherProviderTest::testFlightCategoryDescription()
{
    WeatherProvider *wp = WeatherProvider::instance();

    auto feed = [wp](const QString &category) {
        QJsonObject metar;
        metar[QStringLiteral("rawOb")] = QStringLiteral("KSFO 012053Z 00000KT 1/4SM FG VV001 12/12 A3001");
        metar[QStringLiteral("obsTime")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        metar[QStringLiteral("temp")] = 12.0;
        metar[QStringLiteral("dewp")] = 12.0;
        if (!category.isEmpty())
            metar[QStringLiteral("flightCategory")] = category;
        QJsonArray arr;
        arr.append(metar);
        wp->handleMetarJson(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    };

    feed(QStringLiteral("LIFR"));
    QCOMPARE(wp->weatherDescription(), QStringLiteral("LIFR (low IFR)"));
    QVERIFY(!wp->weatherDescription().contains(QStringLiteral("Thunderstorm")));

    feed(QStringLiteral("VFR"));
    QCOMPARE(wp->weatherDescription(), QStringLiteral("VFR"));

    // A METAR without a category must not keep the previous one.
    feed(QString());
    QVERIFY(wp->weatherDescription() != QStringLiteral("VFR"));
}

void WeatherProviderTest::testTafParsing()
{
    WeatherProvider *wp = WeatherProvider::instance();

    QJsonObject taf;
    taf[QStringLiteral("rawTAF")] = QStringLiteral(
        "KLAX 012053Z 0121/0224 26012KT P6SM SCT040 "
        "TEMPO 0123/0202 5SM -RA OVC020 "
        "FM020200 20008KT P6SM BKN030 "
        "PROB30 0204/0207 3SM +RA "
        "FM021000 25010KT P6SM SCT040"
    );

    QJsonArray arr;
    arr.append(taf);
    QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);

    wp->handleTafJson(data);

    QVERIFY(wp->tafString().contains(QStringLiteral("KLAX")));
    QVERIFY(wp->tafDeteriorating());
    QVERIFY(wp->tafSummary().size() > 0);

    // Check at least one deterioration reason is present
    bool hasReason = false;
    for (const QString &s : wp->tafSummary()) {
        if (s.contains(QStringLiteral("change")) ||
            s.contains(QStringLiteral("convection")) ||
            s.contains(QStringLiteral("wind")) ||
            s.contains(QStringLiteral("IFR")) ||
            s.contains(QStringLiteral("ceiling")))
            hasReason = true;
    }
    QVERIFY(hasReason);
}

void WeatherProviderTest::testIcaoLookup()
{
    // Test with coordinates near KLAX (Los Angeles International)
    QString error;
    QString icao = WeatherProvider::lookupIcao(33.94, -118.41, &error);

    if (icao.isEmpty()) {
        // Network may be unavailable in test environment — verify error is descriptive
        QVERIFY(!error.isEmpty());
        qDebug() << "ICAO lookup unavailable (network?):" << error;
    } else {
        // Should find a nearby airport
        QCOMPARE(icao.toUpper(), QStringLiteral("KLAX"));
    }
}

// Same METAR with obsTime set to now, so it counts as fresh.
static QByteArray freshMetar(const char *json)
{
    QJsonArray arr = QJsonDocument::fromJson(json).array();
    QJsonObject m = arr.first().toObject();
    m[QStringLiteral("obsTime")] = double(QDateTime::currentSecsSinceEpoch());
    arr[0] = m;
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// W4: the live aviationweather.gov format — obsTime in Unix seconds, visib
// as "10+" or a number, gusts in wgst, ceiling from clouds[], fltCat, VRB.
void WeatherProviderTest::testRealMetarFormat()
{
    WeatherProvider *wp = WeatherProvider::instance();

    wp->handleMetarJson(kMetarKBOS);   // 34010G18KT 10SM FEW180 BKN250 08/03
    QCOMPARE(wp->metarTimestamp(), QDateTime::fromSecsSinceEpoch(1792659240, QTimeZone::UTC));
    QVERIFY(qAbs(wp->windSpeed() - 10 * 0.514444) < 0.01);
    QVERIFY(qAbs(wp->windGust() - 18 * 0.514444) < 0.01);
    QVERIFY(qAbs(wp->visibilityKm() - 16.09) < 0.01);       // "10+" SM
    QCOMPARE(wp->ceilingFt(), 25000);                       // BKN250; FEW180 is no ceiling
    QCOMPARE(wp->weatherDescription(), QStringLiteral("VFR"));
    QCOMPARE(wp->stationId(), QStringLiteral("KBOS"));
    QVERIFY(wp->weatherSource().startsWith(QStringLiteral("METAR KBOS, observed 08:54Z")));

    wp->handleMetarJson(kMetarHAAB);   // CAVOK, no clouds, no gusts
    QCOMPARE(wp->ceilingFt(), -1);
    QCOMPARE(wp->ceiling(), QStringLiteral("None"));
    QCOMPARE(wp->windGust(), 0.0);                          // a METAR with no G group: no gusts
    QVERIFY(qAbs(wp->visibilityKm() - 6 * 1.60934) < 0.01); // "6+"
    QCOMPARE(wp->temperature(), 24.0);

    wp->handleMetarJson(kMetarENGM);   // numeric visib, BKN005, IFR
    QVERIFY(qAbs(wp->visibilityKm() - 3.73 * 1.60934) < 0.01);
    QCOMPARE(wp->ceilingFt(), 500);
    QCOMPARE(wp->weatherDescription(), QStringLiteral("IFR"));

    wp->handleMetarJson(kMetarRJTT);   // VRB05KT
    QVERIFY(qIsNaN(wp->windDirection()));
    QVERIFY(wp->windSummary().contains(QStringLiteral("variable")));
    QVERIFY(!wp->metarParsed().join(QString()).contains(QStringLiteral("nan"), Qt::CaseInsensitive));
}

// W2: an Open-Meteo forecast never stamps a METAR time, so the gust check can
// not auto-pass on forecast data; its source says "Forecast".
void WeatherProviderTest::testForecastNeverMakesMetarFresh()
{
    WeatherProvider *wp = WeatherProvider::instance();
    wp->handleForecastJson(kOpenMeteoAddis);
    QVERIFY(!wp->metarFresh());
    QVERIFY(!wp->metarTimestamp().isValid());
    QCOMPARE(wp->temperature(), 23.0);
    QVERIFY(qAbs(wp->windSpeed() - 3.18) < 0.001);          // m/s (W1)
    QVERIFY(qAbs(wp->windGust() - 9.30) < 0.001);
    QVERIFY(qAbs(wp->visibilityKm() - 37.5) < 0.001);
    QCOMPARE(wp->ceilingFt(), -1);
    QVERIFY(wp->weatherSource().startsWith(QStringLiteral("Forecast (Open-Meteo)")));
}

// W3: a fresh METAR (observation) is not overwritten by a later forecast.
void WeatherProviderTest::testForecastDoesNotOverwriteFreshMetar()
{
    WeatherProvider *wp = WeatherProvider::instance();
    wp->handleMetarJson(freshMetar(kMetarKBOS));
    QVERIFY(wp->metarFresh());
    wp->handleForecastJson(kOpenMeteoAddis);
    QVERIFY(qAbs(wp->windSpeed() - 10 * 0.514444) < 0.01);  // still KBOS
    QCOMPARE(wp->temperature(), 8.3);
    QVERIFY(wp->weatherSource().startsWith(QStringLiteral("METAR KBOS")));
}

// W6: a field the source does not send is unknown (NaN / "--"), not 0.
void WeatherProviderTest::testMissingFieldsAreUnknown()
{
    WeatherProvider *wp = WeatherProvider::instance();
    wp->handleMetarJson(R"JSON([{"icaoId":"XXXX","obsTime":1792659240,"rawOb":"METAR XXXX 220854Z AUTO","clouds":[]}])JSON");
    QVERIFY(qIsNaN(wp->temperature()));
    QVERIFY(qIsNaN(wp->windSpeed()));
    QVERIFY(qIsNaN(wp->visibilityKm()));
    QVERIFY(!wp->windOk());
    QVERIFY(wp->currentSummary().contains(QStringLiteral("--")));
    QVERIFY(!wp->currentSummary().contains(QStringLiteral("nan"), Qt::CaseInsensitive));
    QVERIFY(wp->windSummary().isEmpty());
}

UT_REGISTER_TEST(WeatherProviderTest)

#include "WeatherProviderTest.moc"
