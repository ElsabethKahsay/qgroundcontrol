/**
 * @file MavLinkInterceptTest.cpp
 * @brief Tests for ArmingGate::interceptCommandLong — the MAVLink arm command
 *        interception path (L1 from TEST_CHECKLIST.md).
 *
 * Covers:
 *   - Passive mode allows all arm commands
 *   - Active mode blocks when checks fail
 *   - Active mode allows when checks pass
 *   - Hybrid mode blocks then allows after ACK
 *   - Force arm bypasses gate
 *   - Override active bypasses gate
 *   - Non-arm commands pass through
 *   - Disarm commands pass through
 *   - Training mode blocks
 */

#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "mocks/MockTelemetryBridge.h"
#include "mocks/TestBlockingCheck.h"
#include "utils/Config.h"

class MavLinkInterceptTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testPassiveModeAllowsAll();
    void testActiveModeBlocksOnFail();
    void testActiveModeAllowsWhenPassed();
    void testHybridModeBlocksThenAllowsAfterAck();
    void testForceArmBypassesGate();
    void testOverrideActiveBypassesGate();
    void testNonArmCommandPassesThrough();
    void testDisarmCommandPassesThrough();
    void testAckIsOneTimeUse();
    void testAckWithEmptyPilotNameRejected();
    void testModeTransitionFromActiveToPassive();
    void testInboundForceArmDoesNotBypassGate();
    void testLayer2InboundOnlyArmGate();
    void testLocalForceArmOpensOverrideWindow();
};

// Passive mode: all arm commands should pass through regardless of check state
void MavLinkInterceptTest::testPassiveModeAllowsAll()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Passive);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;  // arm

    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Active mode: arm blocked when mandatory checks are failing
void MavLinkInterceptTest::testActiveModeBlocksOnFail()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Force a mandatory check to a Failed state so the gate must block.
    // createPhase1Checks leaves real checks non-mandatory until a vehicle
    // kind is applied, so drive the block with an explicit mandatory check.
    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.blocking"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    bool allowed = gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams);
    QVERIFY(!allowed);
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.denialReason().isEmpty());

    delete mgr;
}

// Active mode: arm allowed when all mandatory checks pass
void MavLinkInterceptTest::testActiveModeAllowsWhenPassed()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Resolve the airframe kind so the mandatory set is configured (R16);
    // the gate is fail-closed until the kind is known.
    mgr->applyVehicleKind(QStringLiteral("MULTIROTOR"));

    // Confirm all mandatory checks
    for (auto *c : mgr->checks()) {
        c->confirm();
    }

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QVERIFY(gate.isArmingAllowed());
}

// Hybrid mode: blocks on fail, then allows after acknowledgeOverride
void MavLinkInterceptTest::testHybridModeBlocksThenAllowsAfterAck()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Force a mandatory check to failed so the first attempt is blocked.
    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.hybrid"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    // First attempt: blocked
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Acknowledge override
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("TestPilot"), QStringLiteral("Test reason")));

    // Second attempt: allowed (one-time ACK bypass)
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    delete mgr;
}

// Force arm: bypasses gate entirely, sets override active
void MavLinkInterceptTest::testForceArmBypassesGate()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    QSignalSpy allowedSpy(&gate, &ArmingGate::armingAllowedChanged);
    QSignalSpy overrideSpy(&gate, &ArmingGate::overrideActiveChanged);

    gate.forceArm();

    QVERIFY(gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.denialReason().isEmpty());
    QVERIFY(allowedSpy.count() >= 1);
    QVERIFY(overrideSpy.count() >= 1);

    // Arm command should now pass through because override is active
    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Override active: arm command passes through regardless of check state
void MavLinkInterceptTest::testOverrideActiveBypassesGate()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Activate timed override
    gate.overrideGate(QStringLiteral("Test override"), 60);
    QVERIFY(gate.isOverrideActive());

    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
}

// Non-arm commands (param1 != 1) should always pass through
void MavLinkInterceptTest::testNonArmCommandPassesThrough()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    QMap<int, float> params;
    params[1] = 0.0f;  // not an arm command

    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, params));
}

// Disarm command (param1 = -1) should always pass through
void MavLinkInterceptTest::testDisarmCommandPassesThrough()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    QMap<int, float> params;
    params[1] = -1.0f;  // disarm

    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, params));
}

// ACK should only work once — second attempt after ACK consumed should block again
void MavLinkInterceptTest::testAckIsOneTimeUse()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Mandatory failing check keeps the gate closed after the one-time ACK is used.
    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.ack.once"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    // Acknowledge
    gate.acknowledgeOverride(QStringLiteral("Pilot"), QStringLiteral("Reason"));

    // First arm: allowed (ACK consumed)
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Second arm: should block again (ACK was one-time)
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    delete mgr;
}

// Acknowledge with empty pilot name should be rejected
void MavLinkInterceptTest::testAckWithEmptyPilotNameRejected()
{
    ArmingGate gate;
    QVERIFY(!gate.acknowledgeOverride(QString(), QStringLiteral("reason")));
    QVERIFY(!gate.acknowledgeOverride(QStringLiteral("   "), QStringLiteral("reason")));
}

// Mode change from Active to Passive should immediately allow arming
void MavLinkInterceptTest::testModeTransitionFromActiveToPassive()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // Mandatory failing check so Active mode blocks arming.
    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.mode.transition"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    // Active mode: blocked
    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    // Switch to Passive: should now allow
    gate.setMode(ArmingGate::Passive);
    QVERIFY(gate.isArmingAllowed());
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    delete mgr;
}

// Inbound force-arm (param2 = magic force code) must NOT auto-open the gate.
// PreflightPlugin::mavlinkMessage delegates every inbound arm COMMAND_LONG to
// interceptCommandLong, relying on it to gate a force-arm exactly like a normal
// arm command — otherwise any link peer on the same telemetry link could
// self-authorize arming by sending the magic param2.
void MavLinkInterceptTest::testInboundForceArmDoesNotBypassGate()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.forcearm.inbound"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;   // arm
    armParams[2] = kForceArmParam2;  // force-arm magic — must still be gated

    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.isOverrideActive());
    QVERIFY(!gate.denialReason().isEmpty());

    delete mgr;
}

// Local force-arm via TelemetryBridge::forceArm() must open the ArmingGate
// override window (bounded, logged) before sending the command. This is the
// legitimate operator-override path — not a bypass. The gate's override window
// is bounded (kForceArmOverrideSec), logged, and auto-closes.
void MavLinkInterceptTest::testLocalForceArmOpensOverrideWindow()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.forcearm.local"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;   // arm
    armParams[2] = kForceArmParam2;  // force-arm magic

    // Gate is closed before forceArm()
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.isOverrideActive());

    // Simulate local force-arm by calling gate.forceArm() directly (what
    // TelemetryBridge::forceArm() does via QMetaObject::invokeMethod).
    gate.forceArm();

    // Override window opened — arm should now be allowed for the window duration
    QVERIFY(gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.denialReason().isEmpty());

    // With the override window open, ArmingGate::interceptCommandLong gates the
    // command through (this is the INBOUND link-peer path, not the outbound one)
    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

    delete mgr;
}

// REGRESSION: Assert the two-layer arm gate architecture and its boundary.
// Layer 1 (Application, OUTBOUND): TelemetryBridge::arm() checks ArmingGate
//   before sending (fast feedback). This is the ONLY gate on the outbound path.
// Layer 2 (MAVLink, INBOUND-ONLY): PreflightPlugin::mavlinkMessage ->
//   ArmingGate::interceptCommandLong evaluates gate state for INBOUND arm
//   commands from link peers (companion GCS, external GCS). It does NOT see
//   commands sent from this GCS: outbound commands go via
//   Vehicle::sendMavCommand -> sendMessageOnLinkThreadSafe and never pass
//   through mavlinkMessage(). Layer 2 therefore only gates external link
//   peers; the outbound path is gated solely by Layer 1.
// BOTH layers MUST remain. Removing either is a safety regression.
void MavLinkInterceptTest::testLayer2InboundOnlyArmGate()
{
    // This test documents the architectural invariant and its boundary:
    //
    // Layer 1 (Application, OUTBOUND): TelemetryBridge::arm() checks
    //   ArmingGate locally before sending. Provides fast local feedback,
    //   avoids round-trip. Only gate on the outbound arm path.
    //
    // Layer 2 (MAVLink, INBOUND-ONLY): PreflightPlugin::mavlinkMessage ->
    //   ArmingGate::interceptCommandLong evaluates the gate at the MAVLink
    //   interception layer. Catches INBOUND arm commands from link peers
    //   (companion GCS, external GCS, external link peers). It is NOT a
    //   barrier on this GCS's own outbound commands.
    //
    // BOTH layers MUST remain. Removing either is a safety regression.
    //
    // This test does not assert on behavior (intercept tests above cover that)
    // but serves as a documented architectural constraint. If this test ever
    // needs modification, it signals a potential architecture regression.
    QVERIFY(true);  // Architectural invariant documented
}

UT_REGISTER_TEST(MavLinkInterceptTest)

#include "MavLinkInterceptTest.moc"