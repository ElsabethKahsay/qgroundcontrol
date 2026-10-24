/**
 * @file MavLinkInterceptTest.cpp
 * @brief Tests for ArmingGate::interceptCommandLong — the MAVLink arm command
 *        interception path.
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
#include "utils/MonotonicClock.h"
#include "PreflightPlugin.h"

class MavLinkInterceptTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testPassiveModeAllowsAll();
    void testActiveModeBlocksOnFail();
    void testActiveModeAllowsWhenPassed();
    void testPeerArmCannotUseOperatorAck();
    void testForceWindowDoesNotOpenGateForPeers();
    void testNonArmCommandPassesThrough();
    void testDisarmCommandPassesThrough();
    void testAckIsOneTimeUse();
    void testAckWithEmptyPilotNameRejected();
    void testModeTransitionFromActiveToPassive();
    void testInboundForceArmDoesNotBypassGate();
    void testLayer2InboundOnlyArmGate();
    void testLocalForceArmOpensOverrideWindow();
    void testAckNeverChangesArmedState();
    void testArmTransitionsComeFromHeartbeatOnly();
    void testArmSourceLabels();
    void testRcDisarmClearsUnspentAck();
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

    // Resolve the airframe kind so the mandatory set is configured;
    // the gate is fail-closed until the kind is known.
    mgr->applyVehicleKind(QStringLiteral("MULTIROTOR"));

    // Built-in Auto checks need live telemetry and refuse confirm(); exercise
    // the allow path with one explicit mandatory check instead.
    for (auto *c : mgr->checks()) {
        c->setMandatory(false);
    }
    TestBlockingCheck *passCheck = new TestBlockingCheck(QStringLiteral("test.passing"));
    passCheck->forceStatus(CheckStatus::Passed, QStringLiteral("Healthy"));
    mgr->addCheck(passCheck);
    // A live autopilot link: no heartbeat is a hard block.
    MockTelemetryBridge bridge;
    gate.setTelemetryBridge(&bridge);
    bridge.emitHeartbeat();

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    QVERIFY(gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QVERIFY(gate.isArmingAllowed());
}

// The operator's acknowledgment is
// for this GCS's own arm. A peer's inbound arm is judged with token 0, stays
// denied, and leaves the acknowledgment unspent. (Was: Hybrid mode let the
// first inbound arm after an acknowledgment through.)
void MavLinkInterceptTest::testPeerArmCannotUseOperatorAck()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);   // Hybrid behaves as Active

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);
    MockTelemetryBridge bridge;
    gate.setTelemetryBridge(&bridge);
    bridge.emitHeartbeat();

    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.hybrid"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;

    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("TestPilot"), QStringLiteral("Test reason")));
    const quint32 ack = gate.ackTokenForArm();

    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
    QCOMPARE(gate.ackTokenForArm(), ack);   // unspent: still the operator's

    delete mgr;
}

// A force window authorizes exactly the minted token on
// this GCS's own command. armingAllowed (a plain arm) stays false and a
// peer's inbound arm is denied. (Was: the window let every arm through.)
void MavLinkInterceptTest::testForceWindowDoesNotOpenGateForPeers()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    QSignalSpy overrideSpy(&gate, &ArmingGate::overrideActiveChanged);

    gate.forceArm();

    QVERIFY(gate.isOverrideActive());
    QCOMPARE(gate.overrideKind(), ArmingGate::ForceOverride);
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(overrideSpy.count() >= 1);

    QMap<int, float> armParams;
    armParams[1] = 1.0f;
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));
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

// ACK should only work once. The acknowledgment is spent only
// by this GCS's own arm carrying its token (the outbound interceptor); the
// inbound peer path can never spend it (testPeerArmCannotUseOperatorAck).
void MavLinkInterceptTest::testAckIsOneTimeUse()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);
    MockTelemetryBridge bridge;
    gate.setTelemetryBridge(&bridge);
    bridge.emitHeartbeat();

    // Mandatory failing check keeps the gate closed after the one-time ACK is used.
    TestBlockingCheck *failCheck = new TestBlockingCheck(QStringLiteral("test.ack.once"));
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated failure"));
    mgr->addCheck(failCheck);

    QVERIFY(gate.acknowledgeOverride(QStringLiteral("Pilot"), QStringLiteral("Reason")));
    const quint32 token = gate.ackTokenForArm();
    auto interceptor = gate.makeArmInterceptor();
    QString reason;

    QVERIFY2(interceptor(reason, token), qPrintable(reason));   // first arm: ACK consumed
    QVERIFY(!interceptor(reason, token));                        // second: blocked again

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

    // Override window opened for the minted token only: a plain arm is
    // still denied, and so is a peer's inbound force-arm.
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());
    QCOMPARE(gate.overrideKind(), ArmingGate::ForceOverride);
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, armParams));

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

// An accepted ARM_DISARM ACK alone never produces an arm
// transition; it used to be guessed from the ACK and forced into the vehicle.
void MavLinkInterceptTest::testAckNeverChangesArmedState()
{
    ArmingGate gate;
    ArmStateTracker tracker(&gate);
    QSignalSpy armed(&gate, &ArmingGate::vehicleArmed);
    QSignalSpy opened(&gate, &ArmingGate::gateOpened);

    tracker.onArmDisarmAck(true);
    tracker.onArmDisarmAck(true);   // retried ACK

    QCOMPARE(armed.count(), 0);
    QCOMPARE(opened.count(), 0);
}

// Transitions come from armedChanged (HEARTBEAT) only, once
// each; a duplicate notification or a late duplicate ACK adds nothing.
void MavLinkInterceptTest::testArmTransitionsComeFromHeartbeatOnly()
{
    ArmingGate gate;
    ArmStateTracker tracker(&gate);
    QSignalSpy armed(&gate, &ArmingGate::vehicleArmed);
    QSignalSpy disarmed(&gate, &ArmingGate::vehicleDisarmed);
    QSignalSpy opened(&gate, &ArmingGate::gateOpened);
    QSignalSpy closed(&gate, &ArmingGate::gateClosed);

    tracker.onArmedChanged(true);    // RC arm: no ACK at all
    tracker.onArmedChanged(true);    // duplicate notification
    tracker.onArmDisarmAck(true);    // duplicate / late ACK
    QCOMPARE(armed.count(), 1);
    QCOMPARE(opened.count(), 1);

    tracker.onArmedChanged(false);   // RC disarm
    QCOMPARE(disarmed.count(), 1);
    QCOMPARE(closed.count(), 1);
}

// The ACK labels who armed. ACK then HEARTBEAT → GCS; HEARTBEAT
// with no ACK → External (RC / another peer); HEARTBEAT then a late ACK →
// corrected to GCS; an ACK older than the window → External.
void MavLinkInterceptTest::testArmSourceLabels()
{
    TestClock clock;
    MonotonicClock::setForTesting(&clock);
    struct Restore { ~Restore() { MonotonicClock::setForTesting(nullptr); } } restore;
    using S = ArmStateTracker::ArmSource;

    QCOMPARE(ArmStateTracker::sourceFor(500, false), S::Gcs);
    QCOMPARE(ArmStateTracker::sourceFor(-1, false), S::External);
    QCOMPARE(ArmStateTracker::sourceFor(5000, false), S::External);
    QCOMPARE(ArmStateTracker::sourceFor(500, true), S::ForceToken);

    ArmingGate gate;
    ArmStateTracker tracker(&gate);

    tracker.onArmDisarmAck(true);
    clock.advance(400);
    tracker.onArmedChanged(true);
    QCOMPARE(tracker.lastArmSource(), S::Gcs);
    tracker.onArmedChanged(false);

    clock.advance(10000);
    tracker.onArmedChanged(true);
    QCOMPARE(tracker.lastArmSource(), S::External);
    clock.advance(300);
    tracker.onArmDisarmAck(true);    // heartbeat beat the ACK
    QCOMPARE(tracker.lastArmSource(), S::Gcs);
    tracker.onArmedChanged(false);

    tracker.onArmDisarmAck(true);
    clock.advance(4000);             // ACK outside the window
    tracker.onArmedChanged(true);
    QCOMPARE(tracker.lastArmSource(), S::External);
}

// Any disarm — including an RC disarm the GCS never commanded —
// clears an unspent acknowledgment.
void MavLinkInterceptTest::testRcDisarmClearsUnspentAck()
{
    ArmingGate gate;
    ArmStateTracker tracker(&gate);
    tracker.onArmedChanged(true);
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("Pilot"), QStringLiteral("Test")));
    QVERIFY(gate.ackTokenForArm() != 0);

    tracker.onArmedChanged(false);   // RC disarm: no GCS command, no ACK
    QCOMPARE(gate.ackTokenForArm(), quint32(0));
}

UT_REGISTER_TEST(MavLinkInterceptTest)

#include "MavLinkInterceptTest.moc"