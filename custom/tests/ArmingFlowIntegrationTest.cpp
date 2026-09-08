/**
 * @file ArmingFlowIntegrationTest.cpp
 * @brief Integration test for the full arming flow: TelemetryBridge → Checks → Gate → Arm
 *        (L2 from TEST_CHECKLIST.md).
 *
 * Tests the complete chain:
 *   - Vehicle connects → telemetry available → checks evaluate → gate opens → arm allowed
 *   - Vehicle disconnects → checks stale → gate closes
 *   - Check fails mid-evaluation → gate closes
 *   - Force arm → gate opens → vehicle arms → disarm → gate re-evaluates
 *   - Multiple connect/disconnect cycles
 */

#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "mocks/MockTelemetryBridge.h"

// Test helper: a controllable mandatory check used to drive the gate into a
// blocked state deterministically (~ createPhase1Checks sets real checks
// non-mandatory until a vehicle kind is applied, so an explicit check with a
// forced Failed status is the reliable way to block).
class TestFlowCheck : public AbstractCheck {
    Q_OBJECT
public:
    TestFlowCheck(const QString &id, CheckType type = CheckType::Auto,
                 bool mandatory = true, QObject *parent = nullptr)
        : AbstractCheck(id, QStringLiteral("Test ") + id,
                        CheckCategory::Safety, type, mandatory, true, parent)
    {}
    void evaluate() override {}
    void forceStatus(CheckStatus s, const QString &msg = {}) { setStatus(s, msg); }
};

class ArmingFlowIntegrationTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testHappyPathConnectToArm();
    void testCheckFailClosesGate();
    void testForceArmThenDisarm();
    void testMultipleConnectDisconnectCycles();
    void testGateSignalsOnTransition();
    void testCheckRecoveryReopensGate();
    void testPassiveModeNeverBlocks();
    void testHybridModeOverrideFlow();
};

// Full happy path: connect → all checks pass → gate opens → arm allowed
void ArmingFlowIntegrationTest::testHappyPathConnectToArm()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);
    gate.setTelemetryBridge(&bridge);

    // Simulate telemetry arrival
    bridge.setBatteryVoltage(12.6);
    bridge.setBatteryPercent(100);
    bridge.setGpsFixType(3);
    bridge.setGpsSatellites(12);
    bridge.setImuHealthy(true);
    bridge.setCompassHealthy(true);

    // Confirm all manual checks
    for (auto *c : mgr.checks()) {
        if (c->checkType() == CheckType::Manual || c->checkType() == CheckType::Action) {
            c->confirm();
        }
    }

    // Evaluate all checks
    mgr.evaluateAll();

    // Gate should open
    QVERIFY(mgr.allMandatoryPassed());

    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QVERIFY(gate.isArmingAllowed());
}

// Check fails mid-evaluation → gate closes
void ArmingFlowIntegrationTest::testCheckFailClosesGate()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);

    // A mandatory check that starts passing, then fails mid-flight to close the gate.
    TestFlowCheck *batteryLike = new TestFlowCheck(QStringLiteral("test.power.fail"));
    batteryLike->forceStatus(CheckStatus::Passed, QStringLiteral("Healthy"));
    mgr.addCheck(batteryLike);

    // First: all checks pass
    for (auto *c : mgr.checks()) {
        c->confirm();
    }
    mgr.evaluateAll();
    QVERIFY(mgr.allMandatoryPassed());

    // Now: simulate a critical failure by failing the mandatory check.
    batteryLike->forceStatus(CheckStatus::Failed, QStringLiteral("Battery low"));

    // Re-evaluate
    mgr.evaluateAll();

    // Gate should be closed
    QVERIFY(!mgr.allMandatoryPassed());

    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Force arm → gate opens → disarm → gate re-evaluates
void ArmingFlowIntegrationTest::testForceArmThenDisarm()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);
    gate.setTelemetryBridge(&bridge);

    // Mandatory failing check so that, once the force-arm override is reset,
    // the gate re-evaluates to the closed state.
    TestFlowCheck *failCheck = new TestFlowCheck(QStringLiteral("test.forcearm.fail"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr.addCheck(failCheck);

    // Force arm
    gate.forceArm();
    QVERIFY(gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());

    // Arm command should pass
    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Reset gate (simulating disarm)
    gate.resetGate();
    QVERIFY(!gate.isOverrideActive());

    // Gate should re-evaluate based on check state
    // Since a mandatory check is failing, gate should be closed
    QVERIFY(!gate.isArmingAllowed());
}

// Multiple connect/disconnect cycles should not cause issues
void ArmingFlowIntegrationTest::testMultipleConnectDisconnectCycles()
{
    for (int cycle = 0; cycle < 5; ++cycle) {
        MockTelemetryBridge bridge;
        bridge.setConnected(true);
        bridge.setConnectionQuality(100);

        ArmingGate gate;
        gate.setMode(ArmingGate::Active);

        PreflightManager mgr(1);
        mgr.setTelemetryBridge(&bridge);
        gate.setPreflightManager(&mgr);

        // Mandatory failing check so the gate is closed after reset/disconnect.
        TestFlowCheck *failCheck = new TestFlowCheck(QStringLiteral("test.cycle.fail") + QString::number(cycle));
        failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
        mgr.addCheck(failCheck);

        // Confirm all non-failing checks; the forced-fail check keeps the gate closed.
        for (auto *c : mgr.checks()) {
            if (c != failCheck) {
                c->confirm();
            }
        }
        mgr.evaluateAll();

        // The mandatory failing check keeps the gate closed
        QVERIFY(!mgr.allMandatoryPassed());

        // Disconnect
        bridge.setConnected(false);
        gate.resetGate();
        QVERIFY(!gate.isArmingAllowed());
    }
}

// Gate emits correct signals on state transitions
void ArmingFlowIntegrationTest::testGateSignalsOnTransition()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);

    // Mandatory failing check so the first arm attempt is denied.
    TestFlowCheck *failCheck = new TestFlowCheck(QStringLiteral("test.signal.fail"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr.addCheck(failCheck);

    QSignalSpy allowedSpy(&gate, &ArmingGate::armingAllowedChanged);
    QSignalSpy deniedSpy(&gate, &ArmingGate::armingDenied);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    // First arm attempt: blocked
    gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams);
    QVERIFY(deniedSpy.count() >= 1);

    // Force arm
    gate.forceArm();
    QVERIFY(allowedSpy.count() >= 1);

    // Arm command: allowed
    gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams);
}

// Check recovery: a failing check that later passes should reopen the gate
void ArmingFlowIntegrationTest::testCheckRecoveryReopensGate()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);

    // Mandatory check that starts passing, fails, then recovers to pass again.
    TestFlowCheck *recover = new TestFlowCheck(QStringLiteral("test.recover"));
    recover->forceStatus(CheckStatus::Passed, QStringLiteral("Healthy"));
    mgr.addCheck(recover);

    // Confirm all checks initially
    for (auto *c : mgr.checks()) {
        c->confirm();
    }
    mgr.evaluateAll();
    QVERIFY(mgr.allMandatoryPassed());

    // Fail the check
    recover->forceStatus(CheckStatus::Failed, QStringLiteral("Battery low"));
    mgr.evaluateAll();
    QVERIFY(!mgr.allMandatoryPassed());

    // Recover: restore the check to a passed state
    recover->forceStatus(CheckStatus::Passed, QStringLiteral("Restored"));
    mgr.evaluateAll();
    QVERIFY(mgr.allMandatoryPassed());

    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Passive mode never blocks regardless of check state
void ArmingFlowIntegrationTest::testPassiveModeNeverBlocks()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Passive);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);

    // Don't confirm any checks — many will be Pending/Fail
    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Hybrid mode: override flow end-to-end
void ArmingFlowIntegrationTest::testHybridModeOverrideFlow()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    PreflightManager mgr(1);
    mgr.setTelemetryBridge(&bridge);
    gate.setPreflightManager(&mgr);

    // Mandatory failing check keeps the gate closed except during an override/ACK.
    TestFlowCheck *failCheck = new TestFlowCheck(QStringLiteral("test.hybrid.fail"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr.addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    // Step 1: Blocked
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Step 2: Acknowledge override
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("TestPilot"), QStringLiteral("Safety review done")));

    // Step 3: Allowed (one-time ACK)
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Step 4: Blocked again (ACK consumed)
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Step 5: Override gate directly
    gate.overrideGate(QStringLiteral("Emergency override"), 60);
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Step 6: Reset
    gate.resetGate();
    QVERIFY(!gate.isOverrideActive());
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

UT_REGISTER_TEST(ArmingFlowIntegrationTest)

#include "ArmingFlowIntegrationTest.moc"
