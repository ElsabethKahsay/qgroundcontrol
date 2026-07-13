#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "CompassOrientationCheck.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"

class CompassOrientationCheckTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testNoTelemetry();
    void testParamNotAvailable();
    void testOrientationNoneAutoPass();
    void testNonDefaultNeedsManualConfirm();
};

void CompassOrientationCheckTest::testNoTelemetry()
{
    MockTelemetryBridge bridge;
    CompassOrientationCheck check(&bridge);
    check.evaluate();
    QVERIFY(check.status() == CheckStatus::Pending ||
            check.status() == CheckStatus::Skipped);
}

void CompassOrientationCheckTest::testParamNotAvailable()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    CompassOrientationCheck check(&bridge);
    check.evaluate();
}

void CompassOrientationCheckTest::testOrientationNoneAutoPass()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    CompassOrientationCheck check(&bridge);
    check.setTestValue(QStringLiteral("param_CAL_MAG0_ROT"), 0.0);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

void CompassOrientationCheckTest::testNonDefaultNeedsManualConfirm()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    CompassOrientationCheck check(&bridge);
    check.setTestValue(QStringLiteral("param_CAL_MAG0_ROT"), 2.0);
    check.evaluate();
    QVERIFY(check.status() != CheckStatus::Passed);
}

UT_REGISTER_TEST(CompassOrientationCheckTest)

#include "CompassOrientationCheckTest.moc"
