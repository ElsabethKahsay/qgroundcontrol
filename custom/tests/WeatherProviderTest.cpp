#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QTemporaryDir>

#include "UnitTest.h"
#include "WeatherProvider.h"
#include "mocks/MockTelemetryBridge.h"

// Real responses (aviationweather.gov /api/data/metar,
// api.open-meteo.com current=...). The current METAR JSON format.
static const char kMetarHAAB[] = R"JSON([{"icaoId":"HAAB","receiptTime":"2026-10-22T08:55:58.741Z","obsTime":1792659600,"reportTime":"2026-10-22T09:00:00.000Z","temp":24,"dewp":5,"wdir":60,"wspd":5,"visib":"6+","altim":1028,"qcField":16,"metarType":"METAR","rawOb":"METAR HAAB 220900 06005KT CAVOK 24/05 Q1028 NOSIG","lat":8.978,"lon":38.799,"elev":2319,"name":"Addis Ababa/Bole Intl, AA, ET","cover":"CAVOK","clouds":[],"fltCat":"VFR"}])JSON";
static const char kMetarKBOS[] = R"JSON([{"icaoId":"KBOS","receiptTime":"2026-10-22T08:58:13.954Z","obsTime":1792659240,"reportTime":"2026-10-22T09:00:00.000Z","temp":8.3,"dewp":2.8,"wdir":340,"wspd":10,"wgst":18,"visib":"10+","altim":1014.6,"slp":1014.5,"qcField":4,"presTend":-1.8,"metarType":"METAR","rawOb":"METAR KBOS 220854Z 34010G18KT 10SM FEW180 BKN250 08/03 A2996 RMK AO2 SLP145 T00830028 56018","lat":42.3606,"lon":-71.0097,"elev":4,"name":"Boston/Logan Intl, MA, US","cover":"BKN","clouds":[{"cover":"FEW","base":18000},{"cover":"BKN","base":25000}],"fltCat":"VFR"}])JSON";
static const char kMetarENGM[] = R"JSON([{"icaoId":"ENGM","receiptTime":"2026-10-22T08:56:15.828Z","obsTime":1792659000,"reportTime":"2026-10-22T09:00:00.000Z","temp":10,"dewp":9,"wdir":110,"wspd":9,"visib":3.73,"altim":1006,"qcField":0,"wxString":"-RA","metarType":"METAR","rawOb":"METAR ENGM 220850Z 11009KT 6000 -RA BKN005 10/09 Q1006 TEMPO BKN008","lat":60.201,"lon":11.08,"elev":204,"name":"Oslo/Gardermoen Arpt, AK, NO","cover":"BKN","clouds":[{"cover":"BKN","base":500}],"fltCat":"IFR"}])JSON";
static const char kMetarRJTT[] = R"JSON([{"icaoId":"RJTT","receiptTime":"2026-10-22T08:40:14.966Z","obsTime":1792657800,"reportTime":"2026-10-22T08:30:00.000Z","temp":21,"dewp":6,"wdir":"VRB","wspd":5,"visib":"6+","altim":1021,"qcField":0,"metarType":"METAR","rawOb":"METAR RJTT 220830Z VRB05KT 9999 FEW030 BKN070 21/06 Q1021 BECMG TL0900 02010KT","lat":35.553,"lon":139.781,"elev":5,"name":"Tokyo/Haneda Intl, 13, JP","cover":"BKN","clouds":[{"cover":"FEW","base":3000},{"cover":"BKN","base":7000}],"fltCat":"VFR"}])JSON";
static const char kOpenMeteoAddis[] = R"JSON({"latitude":9.033392,"longitude":38.771626,"generationtime_ms":0.21159648895263672,"utc_offset_seconds":10800,"timezone":"Africa/Addis_Ababa","timezone_abbreviation":"GMT+3","elevation":2444.0,"current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C","relative_humidity_2m":"%","weather_code":"wmo code","wind_speed_10m":"m/s","wind_direction_10m":"°","wind_gusts_10m":"m/s","visibility":"m"},"current":{"time":"2026-10-22T12:00","interval":900,"temperature_2m":23.0,"relative_humidity_2m":32,"weather_code":2,"wind_speed_10m":3.18,"wind_direction_10m":123,"wind_gusts_10m":9.30,"visibility":37500.00}})JSON";

// An Open-Meteo hourly forecast as fetchForecast() asks for it (Unix times),
// starting at 2026-10-22 09:00Z, one hour per entry of w10.
static const qint64 kT0 = 1792659600;
static QByteArray hourlyForecast(int hours, qint64 currentUtc, double lat = 9.033392, double lon = 38.771626)
{
    QJsonObject hourly;
    QJsonArray time, t2, rh, w10, w80, w120, d10, d80, d120, g10;
    for (int i = 0; i < hours; ++i) {
        time.append(double(kT0 + i * 3600));
        t2.append(20.0 + i);
        rh.append(40.0);
        w10.append(4.0);
        w80.append(6.0);
        w120.append(7.0);
        // Hour 1 turns through north between 10 and 80 m.
        d10.append(i == 1 ? 350.0 : 90.0);
        d80.append(i == 1 ? 10.0 : 100.0);
        d120.append(110.0);
        g10.append(9.0);
    }
    hourly[QStringLiteral("time")] = time;
    hourly[QStringLiteral("temperature_2m")] = t2;
    hourly[QStringLiteral("relative_humidity_2m")] = rh;
    hourly[QStringLiteral("wind_speed_10m")] = w10;
    hourly[QStringLiteral("wind_speed_80m")] = w80;
    hourly[QStringLiteral("wind_speed_120m")] = w120;
    hourly[QStringLiteral("wind_direction_10m")] = d10;
    hourly[QStringLiteral("wind_direction_80m")] = d80;
    hourly[QStringLiteral("wind_direction_120m")] = d120;
    hourly[QStringLiteral("wind_gusts_10m")] = g10;
    QJsonObject current;
    current[QStringLiteral("time")] = double(currentUtc);
    current[QStringLiteral("temperature_2m")] = 20.0;
    current[QStringLiteral("wind_speed_10m")] = 4.0;
    QJsonObject o;
    o[QStringLiteral("latitude")] = lat;
    o[QStringLiteral("longitude")] = lon;
    o[QStringLiteral("elevation")] = 2444.0;
    o[QStringLiteral("utc_offset_seconds")] = 10800;
    o[QStringLiteral("current")] = current;
    o[QStringLiteral("hourly")] = hourly;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

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
    void testSiteForecastRead();
    void testSiteWindAtHeight();
    void testSiteConditions();
    void testSiteWeatherKept();
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
    // Nothing fetched yet = unknown, not a real-looking 0.
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

// A METAR flight category is described by name; LIFR must never read as
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

// The live aviationweather.gov format — obsTime in Unix seconds, visib
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

// An Open-Meteo forecast never stamps a METAR time, so the gust check can
// not auto-pass on forecast data; its source says "Forecast".
void WeatherProviderTest::testForecastNeverMakesMetarFresh()
{
    WeatherProvider *wp = WeatherProvider::instance();
    wp->handleForecastJson(kOpenMeteoAddis);
    QVERIFY(!wp->metarFresh());
    QVERIFY(!wp->metarTimestamp().isValid());
    QCOMPARE(wp->temperature(), 23.0);
    QVERIFY(qAbs(wp->windSpeed() - 3.18) < 0.001);          // m/s
    QVERIFY(qAbs(wp->windGust() - 9.30) < 0.001);
    QVERIFY(qAbs(wp->visibilityKm() - 37.5) < 0.001);
    QCOMPARE(wp->ceilingFt(), -1);
    QVERIFY(wp->weatherSource().startsWith(QStringLiteral("Forecast (Open-Meteo)")));
}

// A fresh METAR (observation) is not overwritten by a later forecast.
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

// A field the source does not send is unknown (NaN / "--"), not 0.
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

// The hourly forecast is read with its times in UTC, whether they come as
// Unix seconds or as local ISO times with the response's UTC offset.
void WeatherProviderTest::testSiteForecastRead()
{
    const SiteWeather::Forecast f = SiteWeather::parseOpenMeteo(hourlyForecast(3, kT0 + 600));
    QVERIFY(f.ok);
    QCOMPARE(f.hours.size(), 3);
    QCOMPARE(f.hours.at(2).utc, kT0 + 7200);
    QCOMPARE(f.issuedUtc, kT0 + 600);
    QCOMPARE(f.groundAmslM, 2444.0);
    QCOMPARE(f.hours.at(2).tempC, 22.0);
    QCOMPARE(f.hours.at(0).windMs[1], 6.0);
    QCOMPARE(f.hours.at(0).gust10Ms, 9.0);

    // Local time 12:00 at UTC+3 is 09:00Z.
    const QByteArray iso = R"JSON({"utc_offset_seconds":10800,"current":{"time":"2026-10-22T12:15"},
        "hourly":{"time":["2026-10-22T12:00"],"wind_speed_10m":[3.0]}})JSON";
    const SiteWeather::Forecast g = SiteWeather::parseOpenMeteo(iso);
    QVERIFY(g.ok);
    QCOMPARE(g.hours.at(0).utc, kT0);
    QCOMPARE(g.issuedUtc, kT0 + 900);
    QVERIFY(qIsNaN(g.hours.at(0).windMs[1]));     // not sent: unknown
    QVERIFY(qIsNaN(g.hours.at(0).tempC));

    // The current-only reply the panels used to get has no hours.
    QVERIFY(!SiteWeather::parseOpenMeteo(kOpenMeteoAddis).ok);
}

// Wind between 10, 80 and 120 m in ln(height); gusts keep their excess.
void WeatherProviderTest::testSiteWindAtHeight()
{
    const SiteWeather::Forecast f = SiteWeather::parseOpenMeteo(hourlyForecast(2, kT0));
    const SiteWeather::Hour &h = f.hours.at(0);
    double v, d, g;
    SiteWeather::windAtHeight(h, 10, &v, &d, &g);
    QCOMPARE(v, 4.0);
    QCOMPARE(g, 9.0);
    SiteWeather::windAtHeight(h, 80, &v, &d, &g);
    QCOMPARE(v, 6.0);
    QCOMPARE(d, 100.0);
    QCOMPARE(g, 11.0);                                   // 6 + (9 - 4)
    SiteWeather::windAtHeight(h, 40, &v, &d, &g);
    QVERIFY(qAbs(v - (4.0 + 2.0 / 3.0 * 2.0)) < 1e-9);   // ln(40/10) / ln(80/10) = 2/3
    SiteWeather::windAtHeight(h, 100, &v, &d, &g);
    QVERIFY(qAbs(v - (6.0 + std::log(100.0 / 80.0) / std::log(1.5))) < 1e-9);
    SiteWeather::windAtHeight(h, 300, &v, &d, &g);
    QCOMPARE(v, 7.0);                                    // above 120 m: the 120 m value
    SiteWeather::windAtHeight(h, 2, &v, &d, &g);
    QCOMPARE(v, 4.0);                                    // below 10 m: the 10 m value

    // 350° at 10 m and 10° at 80 m: half way up (in ln height) is north, not south.
    SiteWeather::windAtHeight(f.hours.at(1), std::sqrt(10.0 * 80.0), &v, &d, &g);
    QVERIFY2(d < 0.5 || d > 359.5, qPrintable(QString::number(d)));

    // The gust limit (the operators' red line: anything above 12 m/s): 11 m/s
    // at 80 m and exactly 12 m/s at 120 m are not above it; 12.5 is.
    // Unknown values never turn red.
    SiteWeather::Conditions c80;
    SiteWeather::windAtHeight(h, 80, &c80.windMs, &c80.windFromDeg, &c80.gustMs);
    SiteWeather::Conditions c120;
    SiteWeather::windAtHeight(h, 120, &c120.windMs, &c120.windFromDeg, &c120.gustMs);
    QCOMPARE(c120.gustMs, 12.0);
    QVERIFY(!SiteWeather::gustAbove(c80, 12.0));
    QVERIFY(!SiteWeather::gustAbove(c120, 12.0));
    QVERIFY(SiteWeather::gustAbove(c120, 11.9));
    QVERIFY(!SiteWeather::gustAbove(c120, qQNaN()));
    QVERIFY(!SiteWeather::gustAbove(SiteWeather::Conditions{}, 12.0));
    SiteWeather::Conditions noGust;
    noGust.windMs = 12.5;                                // no gust known: the mean wind counts
    QVERIFY(SiteWeather::gustAbove(noGust, 12.0));

    // A missing 80 m value: the 10 m value is used up to the next known one.
    SiteWeather::Hour gap = h;
    gap.windMs[1] = qQNaN();
    SiteWeather::windAtHeight(gap, 50, &v, &d, &g);
    QCOMPARE(v, 4.0);
}

// Forecast first, METAR as the stand-in; the temperature is moved to the
// flight height; old, far and missing data lower the confidence with a reason.
void WeatherProviderTest::testSiteConditions()
{
    const SiteWeather::Station haab = SiteWeather::parseMetar(kMetarHAAB);
    QVERIFY(haab.ok);
    QCOMPARE(haab.elevM, 2319.0);
    QVERIFY(qAbs(haab.windMs - 5 * 0.514444) < 1e-9);
    QCOMPARE(haab.gustMs, 0.0);
    QCOMPARE(haab.obsUtc, qint64(1792659600));

    const double lat = 9.033392, lon = 38.771626;
    const SiteWeather::Forecast f = SiteWeather::parseOpenMeteo(hourlyForecast(5, kT0 + 600));

    // Fresh forecast at the site: High; temperature from the forecast ground
    // (2444 m) to the site (2400 m) plus 100 m of flight height.
    SiteWeather::Conditions c = SiteWeather::conditionsAt(f, haab, kT0 + 1200, lat, lon, 2400, 100);
    QVERIFY(c.windKnown);
    QCOMPARE(c.windSource, QStringLiteral("forecast"));
    QCOMPARE(c.confidence, int(SiteWeather::High));
    QVERIFY(c.reasons.isEmpty());
    QVERIFY(qAbs(c.tempC - (20.0 - 0.0065 * (2500.0 - 2444.0))) < 1e-9);
    QCOMPARE(c.forecastHourUtc, kT0);
    QVERIFY(qAbs(c.metarWindMs - 5 * 0.514444) < 1e-9);  // the check value

    // The hour is the one covering the time.
    c = SiteWeather::conditionsAt(f, haab, kT0 + 2 * 3600 + 1800, lat, lon, 2400, 0);
    QCOMPARE(c.forecastHourUtc, kT0 + 2 * 3600);

    // Older than 3 hours: Medium, with the reason.
    c = SiteWeather::conditionsAt(f, haab, kT0 + 3 * 3600 + 1800, lat, lon, 2400, 0);
    QCOMPARE(c.confidence, int(SiteWeather::Medium));
    QVERIFY(c.reasons.join(QString()).contains(QStringLiteral("Old weather")));

    // Made for a point about 18 km away: Medium, with the distance.
    c = SiteWeather::conditionsAt(f, haab, kT0 + 1200, 9.2, lon, 2400, 0);
    QCOMPARE(c.confidence, int(SiteWeather::Medium));
    QVERIFY(c.reasons.join(QString()).contains(QStringLiteral("19 km")));

    // Past the forecast's last hour: the METAR stands in, Low, temperature
    // moved from Bole's height (2319 m) to the site's.
    c = SiteWeather::conditionsAt(f, haab, kT0 + 6 * 3600, lat, lon, 2400, 0);
    QVERIFY(c.windKnown);
    QCOMPARE(c.windSource, QStringLiteral("METAR HAAB"));
    QCOMPARE(c.confidence, int(SiteWeather::Low));
    QVERIFY(qAbs(c.tempC - (24.0 - 0.0065 * (2400.0 - 2319.0))) < 1e-9);
    QVERIFY(c.reasons.join(QString()).contains(QStringLiteral("does not cover")));
    QVERIFY(c.reasons.join(QString()).contains(QStringLiteral("Old weather: METAR")));   // 6 h old
    QVERIFY(c.humidityPct > 0 && c.humidityPct < 100);

    // Site height unknown: the METAR temperature is not moved, and says so.
    c = SiteWeather::conditionsAt(SiteWeather::Forecast{}, haab, kT0 + 600, lat, lon, qQNaN(), 0);
    QCOMPARE(c.tempC, 24.0);
    QVERIFY(c.reasons.join(QString()).contains(QStringLiteral("Site height not known")));

    // Nothing at all: no confidence, no made-up values.
    c = SiteWeather::conditionsAt(SiteWeather::Forecast{}, SiteWeather::Station{}, kT0, lat, lon, 2400, 0);
    QVERIFY(!c.windKnown);
    QCOMPARE(c.confidence, int(SiteWeather::None));
    QVERIFY(qIsNaN(c.windMs));
    QVERIFY(qIsNaN(c.tempC));
}

// What was fetched is kept on disk and read back (a site without internet);
// a bad reply never replaces good data.
void WeatherProviderTest::testSiteWeatherKept()
{
    WeatherProvider *wp = WeatherProvider::instance();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString keep = wp->m_siteFilePath;
    wp->m_siteFilePath = dir.filePath(QStringLiteral("site_weather.json"));
    wp->loadSite();
    QVERIFY(!wp->siteForecast().ok);

    const int rev = wp->siteRevision();
    QSignalSpy spy(wp, &WeatherProvider::siteWeatherChanged);
    wp->keepSiteForecast(hourlyForecast(3, kT0 + 600));
    wp->keepSiteMetar(kMetarHAAB);
    QCOMPARE(spy.count(), 2);
    QVERIFY(wp->siteRevision() > rev);
    wp->keepSiteForecast(kOpenMeteoAddis);              // no hours: ignored
    wp->keepSiteMetar("[]");
    QCOMPARE(spy.count(), 2);
    QCOMPARE(wp->siteForecast().hours.size(), 3);

    // As after a restart.
    wp->m_siteForecast = {};
    wp->m_siteStation = {};
    wp->loadSite();
    QCOMPARE(wp->siteForecast().hours.size(), 3);
    QCOMPARE(wp->siteStation().id, QStringLiteral("HAAB"));
    const SiteWeather::Conditions c = wp->conditionsAt(9.033392, 38.771626, 2400, 80, kT0 + 1200);
    QCOMPARE(c.windMs, 6.0);
    QCOMPARE(c.confidence, int(SiteWeather::High));

    const QVariantMap m = wp->siteConditions(9.033392, 38.771626, 2400, 80);
    QVERIFY(m.value(QStringLiteral("savedText")).toString().contains(QStringLiteral("METAR HAAB")));
    QVERIFY(m.contains(QStringLiteral("reasons")));

    wp->m_siteFilePath = keep;
    wp->loadSite();
}

UT_REGISTER_TEST(WeatherProviderTest)

#include "WeatherProviderTest.moc"
