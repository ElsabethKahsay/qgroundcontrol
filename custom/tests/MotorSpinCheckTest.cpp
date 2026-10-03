#include <QTest>

#include "UnitTest.h"
#include "MotorSpinCheck.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"

// R-05 regression: MotorSpinCheck must not sit in Pending forever when the
// motor test panel is never opened.  A configurable/overridable Pending window
// auto-transitions the check to Failed(TIMEOUT) so the operator is prompted.
class MotorSpinCheckTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testPendingWhilePanelOpenWindow();
    void testAutoTimeoutWhenPanelNeverOpened();
    void testResetRestartsWithFreshWindow();
    void testConfirmPassesBeforeTimeout();
    void testTimeoutNotAppliedAfterPassed();
};

void MotorSpinCheckTest::testPendingWhilePanelOpenWindow()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MotorSpinCheck check(&bridge, nullptr, 60000);
    check.m_telemetry = &bridge;

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);
    QVERIFY2(check.message().contains(QStringLiteral("motor test panel")),
             qPrintable(QStringLiteral("unexpected message: %1").arg(check.message())));
    QTest::qWait(10);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);
}

void MotorSpinCheckTest::testAutoTimeoutWhenPanelNeverOpened()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MotorSpinCheck check(&bridge, nullptr, 60);
    check.m_telemetry = &bridge;

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);

    QTest::qWait(120);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
    QVERIFY2(check.message().contains(QStringLiteral("TIMEOUT")),
             qPrintable(QStringLiteral("expected TIMEOUT message, got: %1").arg(check.message())));
}

void MotorSpinCheckTest::testResetRestartsWithFreshWindow()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MotorSpinCheck check(&bridge, nullptr, 60);
    check.m_telemetry = &bridge;

    check.evaluate();
    QTest::qWait(120);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);

    check.reset();
    QCOMPARE(check.status(), CheckStatus::Pending);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);
    QTest::qWait(120);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
}

void MotorSpinCheckTest::testConfirmPassesBeforeTimeout()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MotorSpinCheck check(&bridge, nullptr, 60000);
    check.m_telemetry = &bridge;

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);

    QVERIFY(check.confirm(QStringLiteral("Motor test completed")));
    QCOMPARE(check.status(), CheckStatus::Passed);

    QTest::qWait(120);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

void MotorSpinCheckTest::testTimeoutNotAppliedAfterPassed()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    MotorSpinCheck check(&bridge, nullptr, 40);
    check.m_telemetry = &bridge;

    check.evaluate();
    QVERIFY(check.confirm(QStringLiteral("Motor test completed")));
    QTest::qWait(100);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

UT_REGISTER_TEST(MotorSpinCheckTest)

#include "MotorSpinCheckTest.moc"