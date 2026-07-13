#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "AbstractCheck.h"
#include "RcCalibrationCheck.h"
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
    check.setTestValue(QStringLiteral("RC2_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC2_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC3_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC3_MAX"), 2000.0);
    check.setTestValue(QStringLiteral("RC4_MIN"), 1000.0);
    check.setTestValue(QStringLiteral("RC4_MAX"), 2000.0);

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

UT_REGISTER_TEST(RcCalibrationCheckTest)

#include "RcCalibrationCheckTest.moc"
