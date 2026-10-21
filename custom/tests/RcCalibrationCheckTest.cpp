#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "AbstractCheck.h"
#include "RcCalibrationCheck.h"
#include "RcThrottleMinCheck.h"
#include "RcFailsafeCheck.h"
#include "RcRssiCheck.h"
#include "mocks/MockTelemetryBridge.h"

class RcCalibrationCheckTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testFactoryDefaultReturnsWarning();
    void testCalibratedRangePasses();
    void testShortRangeReturnsWarning();
    void testNoTelemetry();
    void testTrimFarFromCenter();
    void testChannel0NoSignalFallback();
    void testThrottleTrimNotJudgedAgainstCenter();
    void testThrottleMinPassesOnlyAtBottom();
    void testThrottleMinReversedChannel();
    void testUnreportedRssiIsNotFailsafe();
};

void RcCalibrationCheckTest::testFactoryDefaultReturnsWarning()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    RcCalibrationCheck check(&bridge);

    check.setTestValue(QStringLiteral("RC1_MIN"), 1100.0);
    check.setTestValue(QStringLiteral("RC1_MAX"), 1900.0);
    check.setTestValue(QStringLiteral("RC2_MIN"), 1100.0);
    check.setTestValue(QStringLiteral("RC2_MAX"), 1900.0);
    check.setTestValue(QStringLiteral("RC3_MIN"), 1100.0);
    check.setTestValue(QStringLiteral("RC3_MAX"), 1900.0);
    check.setTestValue(QStringLiteral("RC4_MIN"), 1100.0);
    check.setTestValue(QStringLiteral("RC4_MAX"), 1900.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY(check.message().contains(QStringLiteral("factory defaults")));
}

void RcCalibrationCheckTest::testCalibratedRangePasses()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    RcCalibrationCheck check(&bridge);

    check.setTestValue(QStringLiteral("RC1_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC1_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC2_MIN"), 950.0);
    check.setTestValue(QStringLiteral("RC2_MAX"), 1950.0);
    check.setTestValue(QStringLiteral("RC3_MIN"), 1050.0);
    check.setTestValue(QStringLiteral("RC3_MAX"), 2050.0);
    check.setTestValue(QStringLiteral("RC4_MIN"), 980.0);
    check.setTestValue(QStringLiteral("RC4_MAX"), 1980.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

void RcCalibrationCheckTest::testShortRangeReturnsWarning()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    RcCalibrationCheck check(&bridge);

    // min too high (1400 > 1200), max too low (1600 < 1800)
    check.setTestValue(QStringLiteral("RC1_MIN"), 1400.0);
    check.setTestValue(QStringLiteral("RC1_MAX"), 1600.0);
    check.setTestValue(QStringLiteral("RC2_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC2_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC3_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC3_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC4_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC4_MAX"), 2000.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY(check.message().contains(QStringLiteral("Ch1")));
}

void RcCalibrationCheckTest::testNoTelemetry()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(false);

    RcCalibrationCheck check(&bridge);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);
}

void RcCalibrationCheckTest::testTrimFarFromCenter()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    RcCalibrationCheck check(&bridge);

    // Calibrated endpoints but Ch1 trim far from center
    check.setTestValue(QStringLiteral("RC1_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC1_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC1_TRIM"), 1100.0);
    check.setTestValue(QStringLiteral("RC2_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC2_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC3_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC3_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC4_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC4_MAX"), 2000.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY(check.message().contains(QStringLiteral("trim")));
}

void RcCalibrationCheckTest::testChannel0NoSignalFallback()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    // Set ch1 = 0 (no signal), others valid — triggers fallback path Warning
    bridge.setRcChannelValues({0, 1500, 1500, 1500});

    RcCalibrationCheck check(&bridge);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY(check.message().contains(QStringLiteral("not responding")));
}

// ArduPilot puts RC3_TRIM (throttle) at the bottom of the stick after
// calibration; that must not read as "trim far from center".
void RcCalibrationCheckTest::testThrottleTrimNotJudgedAgainstCenter()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    RcCalibrationCheck check(&bridge);
    for (int ch = 1; ch <= 4; ++ch) {
        check.setTestValue(QStringLiteral("RC%1_MIN").arg(ch), 1000.0);
        check.setTestValue(QStringLiteral("RC%1_MAX").arg(ch), 2000.0);
        check.setTestValue(QStringLiteral("RC%1_TRIM").arg(ch), ch == 3 ? 1000.0 : 1500.0);
    }
    check.evaluate();
    QVERIFY2(check.status() == CheckStatus::Passed, qPrintable(check.message()));

    check.setTestValue(QStringLiteral("RC1_TRIM"), 1100.0);   // roll trim far off: still caught
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
}

// The throttle check passes only with the stick at the bottom of its range
// (it used to pass for any value >= 900 µs).
void RcCalibrationCheckTest::testThrottleMinPassesOnlyAtBottom()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    RcThrottleMinCheck check(&bridge);
    check.setTestValue(QStringLiteral("param_RC3_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("param_RC3_MAX"), 2000.0);

    bridge.setRcChannelValues({1500, 1500, 1050, 1500});
    check.evaluate();
    QVERIFY2(check.status() == CheckStatus::Passed, qPrintable(check.message()));

    bridge.setRcChannelValues({1500, 1500, 1500, 1500});
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);

    bridge.setRcChannelValues({1500, 1500, 1950, 1500});
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
}

void RcCalibrationCheckTest::testThrottleMinReversedChannel()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    RcThrottleMinCheck check(&bridge);
    check.setTestValue(QStringLiteral("param_RC3_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("param_RC3_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("param_RC3_REVERSED"), 1.0);

    bridge.setRcChannelValues({1500, 1500, 1960, 1500});
    check.evaluate();
    QVERIFY2(check.status() == CheckStatus::Passed, qPrintable(check.message()));

    bridge.setRcChannelValues({1500, 1500, 1050, 1500});
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
}

// RSSI not reported (ArduPilot RSSI_TYPE = 0 → bridge rcRssi = -1) with a live
// link must not fail the RC failsafe or RSSI checks.
void RcCalibrationCheckTest::testUnreportedRssiIsNotFailsafe()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setRcConnected(true);
    bridge.setRcFailsafe(false);
    bridge.setRcRssi(-1);

    RcFailsafeCheck failsafe(&bridge, 50);
    failsafe.evaluate();
    QVERIFY2(failsafe.status() == CheckStatus::Passed, qPrintable(failsafe.message()));

    RcRssiCheck rssi(&bridge, 50, 50);
    rssi.evaluate();
    QVERIFY2(rssi.status() != CheckStatus::Failed && rssi.status() != CheckStatus::Warning,
             qPrintable(rssi.message()));

    bridge.setRcRssi(20);                       // a MEASURED low RSSI still fails
    failsafe.evaluate();
    QCOMPARE(failsafe.status(), CheckStatus::Failed);
}

UT_REGISTER_TEST(RcCalibrationCheckTest)

#include "RcCalibrationCheckTest.moc"
