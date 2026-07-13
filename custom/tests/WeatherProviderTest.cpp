#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "UnitTest.h"
#include "WeatherProvider.h"
#include "MetarCeilingCheck.h"
#include "mocks/MockTelemetryBridge.h"

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
    void testTafParsing();
    void testIcaoLookup();
    void testApiTimeout();
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
    QCOMPARE(wp->visibilityKm(), 0.0);
    QCOMPARE(wp->ceilingFt(), 0);
}

void WeatherProviderTest::testMetarParsing()
{
    WeatherProvider *wp = WeatherProvider::instance();

    QJsonObject metar;
    metar[QStringLiteral("rawOb")] = QStringLiteral("KLAX 012053Z 26012KT 10SM FEW040 20/15 A2992");
    metar[QStringLiteral("obsTime")] = QStringLiteral("2026-01-01T20:53:00Z");
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

void WeatherProviderTest::testApiTimeout()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MetarCeilingCheck check(&bridge);
    QCOMPARE(check.status(), CheckStatus::Pending);

    check.evaluate();

    if (check.status() == CheckStatus::Pending) {
        // Fetch was triggered — access m_lastEvalTime via friend to simulate timeout
        check.m_lastEvalTime = QDateTime::currentDateTime().addSecs(-12);

        // Second evaluate should hit timeout path since metarFresh() is false
        check.evaluate();

        // The check should NOT return Failed (would block arming)
        QVERIFY(check.status() != CheckStatus::Failed);

        // It should return Skipped (non-blocking) or Pending
        QVERIFY(check.status() == CheckStatus::Skipped ||
                check.status() == CheckStatus::Pending);

        if (check.status() == CheckStatus::Skipped) {
            // Verify the message explains the data unavailability
            QVERIFY(!check.message().isEmpty());
        }
    } else {
        // May already be Skipped if weather is disabled
        QVERIFY(check.status() == CheckStatus::Skipped ||
                check.status() == CheckStatus::Pending);
    }
}

UT_REGISTER_TEST(WeatherProviderTest)

#include "WeatherProviderTest.moc"
