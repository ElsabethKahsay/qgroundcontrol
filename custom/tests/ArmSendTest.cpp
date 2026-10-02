/**
 * @file ArmSendTest.cpp
 * @brief Wire-level arm send tests: verify TelemetryBridge::arm()/forceArm()
 *        actually send (or suppress) MAV_CMD_COMPONENT_ARM_DISARM over a real
 *        MockLink, using MockLink's received-command counter as the oracle.
 *
 * The gate enforcement now lives on the Vehicle (Vehicle::_sendMavCommandWorker
 * consults the arm interceptor), so each scenario installs the gate's
 * interceptor on the vehicle explicitly — under the unit-test harness the
 * PreflightPlugin skips auto-install so direct-arm tests are unaffected.
 *
 * Four scenarios:
 *   (a) failing critical check (gate closed) -> arm() sends nothing, vehicle stays disarmed
 *   (b) forceArm() then arm()               -> exactly one arm command, vehicle arms
 *   (c) override expires (setOverrideDuration) -> arm() blocked again, no new command
 *   (d) stock setArmed path with gate closed -> vetoed: nothing on the wire, not armed
*  (e) no interceptor installed (no-op)    -> stock setArmed arm is untouched: exactly one
 *                                              command with param1=1, vehicle arms
 *  (f) disarm(false) path does not arm     -> VehicleProfileManager::disarmVehicle(false)
 *                                              sends param1=0 (disarm), never an arm payload
 *  (g) production wiring installs the veto -> TelemetryBridge::setVehicle() and
 *      setArmingGate() install the interceptor as a SIDE EFFECT (the same chain
 *      PreflightPlugin::_setupForVehicle + init run), not via this file's manual
 *      _makeBlockedGate() install. Closes the runningUnitTests() wiring gap.
 */

#include <QTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QCoreApplication>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "TelemetryBridge.h"
#include "mocks/MockTelemetryBridge.h"
#include "MultiVehicleManager.h"
#include "VehicleProfileManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MAVLinkProtocol.h"
#include "Comms/MockLink/MockLink.h"
#include "utils/Config.h"

// Test helper check used to drive the gate into a blocked state deterministically.
class ArmSendFlowCheck : public AbstractCheck {
    Q_OBJECT
public:
    ArmSendFlowCheck(const QString &id, QObject *parent = nullptr)
        : AbstractCheck(id, QStringLiteral("Test ") + id,
                        CheckCategory::Safety, CheckType::Auto, true, true, parent)
    {}
    void evaluate() override {}
    void forceStatus(CheckStatus s, const QString &msg = {}) { setStatus(s, msg); }
};

class ArmSendTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testArmBlockedSendsNothing();
    void testForceArmThenArmSendsOne();
    void testForceArmExpiresThenArmBlocked();
    void testStockSetArmedBypassesGate();
    void testNoInterceptorStockBehaviorUnchanged();
    void testDisarmSendsDisarmNotArm();
    void testProductionWiringInstallsInterceptor();
    void testBenchForceArmFunnelSingle2989Command();
    void testForceArmRefusedAirborne();
    void testForceArmRefusalTextIsSubstituted();
    void testStockChainDeniedInsideBenchOverrideWindow();
    void testSameTokenSecondArmDeniedThenFreshMintArms();

private:
    bool _pumpUntil(bool (*cond)(void* ctx), void* ctx, int timeoutMs);
    Vehicle* _connectPlane();
    void _makeBlockedGate(ArmingGate &gate, MockTelemetryBridge &bridge);
    struct FunnelProbe {
        MockLink* mockLink = nullptr;
        Vehicle*  vehicle  = nullptr;
    };
    static bool _sawParam2(void* ctx) {
        return !qIsNaN(static_cast<FunnelProbe*>(ctx)->mockLink->lastArmDisarmParam2());
    }
    static bool _wireCount(void* ctx) {
        return static_cast<FunnelProbe*>(ctx)->mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }
};

bool ArmSendTest::_pumpUntil(bool (*cond)(void* ctx), void* ctx, int timeoutMs)
{
    const quint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (cond(ctx)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(50);
    }
    return cond(ctx);
}

Vehicle* ArmSendTest::_connectPlane()
{
    _mockLink = MockLink::startAPMArduPlaneMockLink(false);
    if (!_mockLink) return nullptr;
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    if (!spyVehicle.wait(10000)) return nullptr;
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!_vehicle) return nullptr;
    QSignalSpy spyPlan(_vehicle, &Vehicle::initialConnectComplete);
    if (!spyPlan.wait(30000)) return nullptr;
    return _vehicle;
}

// Wire a fresh gate stuck in a "critical check failed" (closed) state, the same
// harness used by ArmingFlowIntegrationTest: a mandatory check forced to Failed.
// The PreflightManager must outlive the gate (the gate's periodic timer queries
// it), so it is heap-allocated and parented to the gate.
// Installs the gate's arm interceptor on the connected vehicle so the stock arm
// path exercises the same veto as live (where PreflightPlugin installs it).
void ArmSendTest::_makeBlockedGate(ArmingGate &gate, MockTelemetryBridge &bridge)
{
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    mgr->setTelemetryBridge(&bridge);
    gate.setPreflightManager(mgr);
    gate.setTelemetryBridge(&bridge);

    ArmSendFlowCheck *failCheck = new ArmSendFlowCheck(QStringLiteral("test.armsend.fail"), mgr);
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated critical failure"));
    mgr->addCheck(failCheck);

    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.isOverrideActive());

    _vehicle->setArmCommandInterceptor(gate.makeArmInterceptor());
}

// (a) Gate closed -> TelemetryBridge::arm() sends nothing, vehicle stays disarmed.
void ArmSendTest::testArmBlockedSendsNothing()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed()); // start disarmed

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    _mockLink->clearReceivedMavCommandCounts();

    bridge.arm();
    QTest::qWait(500); // give any (incorrect) send time to reach the link

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(!_vehicle->armed()); // still disarmed
    QVERIFY(!gate.isArmingAllowed());
}

// (b) forceArm() then arm() -> exactly one arm command, vehicle arms.
void ArmSendTest::testForceArmThenArmSendsOne()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    _mockLink->clearReceivedMavCommandCounts();

    // forceArm() opens the override window then sends.
    bridge.forceArm();
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.isArmingAllowed());

    // arm() while override active -> command must go out.
    bridge.arm();

    QVERIFY(_pumpUntil([](void* ctx) {
        MockLink* link = static_cast<MockLink*>(ctx);
        return link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);

    // The mock link responds by arming; the vehicle must report armed.
    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
}

// (c) Override window expires (setOverrideDuration 200 ms) -> arm() blocked again.
void ArmSendTest::testForceArmExpiresThenArmBlocked()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    _mockLink->clearReceivedMavCommandCounts();

    bridge.forceArm();
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.isArmingAllowed());

    bridge.arm();
    QVERIFY(_pumpUntil([](void* ctx) {
        MockLink* link = static_cast<MockLink*>(ctx);
        return link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);

    // Shorten the override window to 200 ms, then wait for expiry.
    gate.setOverrideDuration(200);
    QTest::qWait(600); // > 200 ms

    QVERIFY(!gate.isOverrideActive());
    QVERIFY(!gate.isArmingAllowed());

    const int before = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);

    // Gate closed again -> arm() must be suppressed; the counter is unchanged.
    bridge.arm();
    QTest::qWait(500);

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), before);
}

// (d) STOCK arm path (Vehicle::setArmed) with a failing critical check.
//     The stock path sends the arm command through Vehicle::_sendMavCommandWorker,
//     which now consults the installed arm interceptor: with the gate closed the
//     command is vetoed locally (MAV_RESULT_DENIED), nothing reaches the wire,
//     and the vehicle stays disarmed. This is the gated behavior.
void ArmSendTest::testStockSetArmedBypassesGate()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    QVERIFY(!gate.isArmingAllowed());

    _mockLink->clearReceivedMavCommandCounts();

    // Stock QML path: GuidedActionsController.qml:636 _activeVehicle.armed = true
    // -> Vehicle::setArmedShowError -> Vehicle::setArmed -> sendMavCommand.
    _vehicle->setArmed(true, false);
    QTest::qWait(500);

    QVERIFY2(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) == 0 &&
             !_vehicle->armed(),
             qPrintable(QStringLiteral("armCount=%1 armed=%2 usedByUnexpectedGate")
                            .arg(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM))
                            .arg(_vehicle->armed() ? "true" : "false")));
}

// (e) NO interceptor installed: the stock arm path must be byte-for-byte identical
//     to upstream QGC. This is the fail-open baseline that proves the veto only
//     exists when an interceptor is installed (no-op case).
void ArmSendTest::testNoInterceptorStockBehaviorUnchanged()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    // Deliberately do NOT install any interceptor: this is the no-op case.
    _mockLink->clearReceivedMavCommandCounts();

    _vehicle->setArmed(true, false);
    QVERIFY(_pumpUntil([](void* ctx) {
        MockLink* link = static_cast<MockLink*>(ctx);
        return link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 1.0f);

    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
}

// (f) disarmVehicle(false) must send a disarm (param1=0), never an arm payload.
//     Regression for the latent arm-via-disarm bug (disarmVehicle(false) used to
//     call setArmedShowError(true)).
void ArmSendTest::testDisarmSendsDisarmNotArm()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    VehicleProfileManager vpm;
    _mockLink->clearReceivedMavCommandCounts();

    vpm.disarmVehicle(false);
    QTest::qWait(500);

    QVERIFY(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1);
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 0.0f);
    QVERIFY(!_vehicle->armed());
}

// (g) PRODUCTION wiring: TelemetryBridge::setVehicle() -> setArmingGate() install
//     the arm interceptor as a SIDE EFFECT (_installArmInterceptor), exactly the
//     chain PreflightPlugin::_setupForVehicle + PreflightPlugin::init run against
//     the real app. This proves the veto is installed by production wiring — not
//     by this file's manual _makeBlockedGate() install.
//
//     The mock opts INTO the install path via the _skipArmInterceptorInstall test
//     seam (the unit-test harness skips the install by default so the direct-arm
//     tests in (a)-(f) are unaffected).
void ArmSendTest::testProductionWiringInstallsInterceptor()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed()); // start disarmed

    MockTelemetryBridge bridge;
    bridge.setSkipArmInterceptorInstall(false); // opt into production install path
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    // Production call #1 (PreflightPlugin::_setupForVehicle):
    // TelemetryBridge::setVehicle() -> _installArmInterceptor(). No gate yet.
    bridge.setVehicle(_vehicle);

    // No gate => the side-effect install must be a no-op: stock arm sends exactly
    // one arm command (fails open). No manual interceptor installed anywhere.
    _mockLink->clearReceivedMavCommandCounts();
    _vehicle->setArmed(true, false);
    QVERIFY(_pumpUntil([](void* ctx) {
        MockLink* link = static_cast<MockLink*>(ctx);
        return link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);

    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());

    // Disarm so the gate-closed phase below starts from disarmed.
    _vehicle->setArmed(false, false);
    QVERIFY(_pumpUntil([](void* ctx) {
        return !static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(!_vehicle->armed());

    // A production-shaped closed gate: mandatory safety check forced to Failed.
    // Deliberately do NOT call _vehicle->setArmCommandInterceptor here — the
    // side effect must install the veto.
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);
    PreflightManager *mgr = new PreflightManager(1, &gate);
    mgr->setTelemetryBridge(&bridge);
    gate.setPreflightManager(mgr);
    gate.setTelemetryBridge(&bridge);
    ArmSendFlowCheck *check = new ArmSendFlowCheck(QStringLiteral("test.armsend.wiring"), mgr);
    check->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated critical failure"));
    mgr->addCheck(check);
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.isOverrideActive());

    // Production call #2 (PreflightPlugin::init):
    // TelemetryBridge::setArmingGate() -> _installArmInterceptor() -> the gate's
    // predicate is now installed on the vehicle as a side effect.
    bridge.setArmingGate(&gate);

    // Stock arm path with the side-effect-installed veto: nothing reaches the wire.
    _mockLink->clearReceivedMavCommandCounts();
    _vehicle->setArmed(true, false);
    QTest::qWait(500); // give any (incorrect) send time to reach the link
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(!_vehicle->armed());

    // The sanctioned path (forceArm opens the override window, arm() sends) must
    // still go through exactly once under the same side-effect-installed veto.
    bridge.forceArm();
    QVERIFY(gate.isOverrideActive());
    bridge.arm();
    QVERIFY(_pumpUntil([](void* ctx) {
        MockLink* link = static_cast<MockLink*>(ctx);
        return link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);

    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
}

// (g) Bench-panel force-arm through the funnel with a CLOSED gate: exactly one
// arm command reaches the wire, carrying the canonical 2989 param2 (never the
// legacy 21196), the bounded override is open, and the vehicle arms.
void ArmSendTest::testBenchForceArmFunnelSingle2989Command()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);
    QVERIFY(!gate.isArmingAllowed());   // gate genuinely closed

    _mockLink->clearReceivedMavCommandCounts();

    FunnelProbe probe{ _mockLink, _vehicle };
    const QString refusal = bridge.forceArm();   // funnel: override + one send

    QVERIFY2(refusal.isEmpty(), qPrintable(QStringLiteral("funnel refused on the ground: %1").arg(refusal)));
    QVERIFY(gate.isOverrideActive());            // bounded override opened
    QVERIFY(gate.isArmingAllowed());
    QVERIFY(_pumpUntil(&ArmSendTest::_wireCount, &probe, 10000));

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 1.0f);   // arm, not disarm

    QVERIFY(_pumpUntil(&ArmSendTest::_sawParam2, &probe, 10000));
    QCOMPARE(_mockLink->lastArmDisarmParam2(), kForceArmParam2);          // 2989
    QVERIFY(_mockLink->lastArmDisarmParam2() != kLegacyForceArmParam2);   // never 21196

    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<Vehicle*>(ctx)->armed();
    }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
}

// (h) Ground-safety precondition: with the vehicle airborne, the funnel REFUSES —
// no override window is opened and nothing reaches the wire.
void ArmSendTest::testForceArmRefusedAirborne()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    // Airborne: ground speed above the bench threshold.
    bridge.setGroundSpeed(static_cast<double>(kForceArmMaxGroundSpeedMs) + 3.0);

    _mockLink->clearReceivedMavCommandCounts();

    const QString refusal = bridge.forceArm();

    QVERIFY2(!refusal.isEmpty(), "force-arm accepted an airborne vehicle");
    QVERIFY2(!gate.isOverrideActive(), "override window opened for a refused airborne force-arm");

    QTest::qWait(500); // give any (incorrect) send time to reach the link
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(!_vehicle->armed());

    // Back on the ground the same funnel accepts (override + one 2989 send).
    bridge.setGroundSpeed(0.0);
    const QString accepted = bridge.forceArm();
    QVERIFY2(accepted.isEmpty(), qPrintable(QStringLiteral("grounded force-arm refused: %1").arg(accepted)));
    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<MockLink*>(ctx)->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->lastArmDisarmParam2(), kForceArmParam2);
}

// (i) The refusal text is operator-facing, so its placeholders must actually
// be substituted. This caught a shipped bug: QString::arg takes %1/%2, but the
// airborne message was written printf-style (%.1f), so the operator would have
// been shown the literal "%.1f" instead of the measured ground speed. Asserting
// only !refusal.isEmpty() passed regardless.
void ArmSendTest::testForceArmRefusalTextIsSubstituted()
{
    QVERIFY(_connectPlane());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    // Airborne at a known speed so the substituted numbers are checkable.
    const double gs = static_cast<double>(kForceArmMaxGroundSpeedMs) + 3.0;
    bridge.setGroundSpeed(gs);

    const QString refusal = bridge.forceArm();
    QVERIFY2(!refusal.isEmpty(), "expected an airborne refusal");

    // No un-substituted printf placeholder may survive.
    // No un-substituted placeholder of either style may survive: the printf
    // form (%.1f) is the actual bug, the Qt form (%1) would also be a failure.
    QVERIFY2(!refusal.contains(QStringLiteral("%")),
             qPrintable(QStringLiteral("refusal still contains an unsubstituted placeholder: %1").arg(refusal)));

    // The measured ground speed (4.0 m/s: 1.0 threshold + 3.0) must appear.
    QVERIFY2(refusal.contains(QString::number(gs, 'f', 1)),
             qPrintable(QStringLiteral("refusal does not contain the measured ground speed %1: %2")
                            .arg(QString::number(gs, 'f', 1)).arg(refusal)));

    // And the reason must name the cause, not just the numbers.
    QVERIFY2(refusal.contains(QStringLiteral("airborne"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("refusal does not name the cause: %1").arg(refusal)));
}

// (j) The bench force-arm override is ONE-SHOT and CALLER-SCOPED. The sanctioned
//     call (TelemetryBridge::forceArm) mints a token for its OWN pending command
//     and consumes it on success; the window stays open for kForceArmOverrideSec
//     but it is NOT ambient. The real-signal-chain terminal of
//     mainWindow.forceArmVehicleRequest (Vehicle::forceArm(), wire-identical:
//     param1=1 param2=2989) holds no token, so inside the still-open window it
//     must be vetoed locally with MAV_RESULT_DENIED and nothing reaching the wire.
//     This fail against the pre-fix ambient-window gate (wire=1, no denial) and
//     passes only once the override is caller-scoped. Covers the stock chain
//     without needing the QML dialog: Vehicle::forceArm() IS the stock handler
//     body for ForceArmVehicleRequest.
void ArmSendTest::testStockChainDeniedInsideBenchOverrideWindow()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);
    QVERIFY(!gate.isArmingAllowed());        // gate genuinely closed

    // Sanctioned bench override: opens the window and mints the one-shot token
    // for its own command.
    const QString refusal = bridge.forceArm();
    QVERIFY2(refusal.isEmpty(), qPrintable(QStringLiteral("funnel refused on the ground: %1").arg(refusal)));
    QVERIFY(gate.isOverrideActive());        // window open
    QVERIFY(gate.isArmingAllowed());

    // The sanctioned command arms through its own token; pump until its ACK
    // lands so the Vehicle duplicate-command guard is clear and a later zero
    // wire count can only mean an interceptor veto, never a dropped duplicate.
    QVERIFY(_pumpUntil([](void* ctx) { return static_cast<Vehicle*>(ctx)->armed(); }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());

    _mockLink->clearReceivedMavCommandCounts();

    // Stock entry-point terminal (what mainWindow.forceArmVehicleRequest ->
    // GuidedActionsController -> handler would invoke). Same params as the
    // sanctioned send but it presents NO token: it must be denied.
    QSignalSpy denySpy(_vehicle, &Vehicle::mavCommandResult);
    _vehicle->forceArm();

    QTest::qWait(1500);   // give an (incorrect) ambient send time to reach the link
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(qIsNaN(_mockLink->lastArmDisarmParam2()));

    bool sawDenied = false;
    const QList<QList<QVariant>> deny = denySpy;
    for (const QList<QVariant> &args : deny) {
        if (args.size() >= 4 && args.at(2).toInt() == MAV_CMD_COMPONENT_ARM_DISARM &&
            args.at(3).toInt() == MAV_RESULT_DENIED) {
            sawDenied = true;
            break;
        }
    }
    QVERIFY2(sawDenied, "stock force-arm inside the bench override window was NOT denied "
                        "(override must be caller-scoped, not ambient)");
}

// (k) The bench force-arm token is STRICTLY ONE-SHOT — not reusable-per-caller.
//     INTENDED DESIGN: each operator arm requires ONE deliberate confirmation; a
//     second arm inside the same window that re-presents the SAME (now-consumed)
//     token is denied locally with MAV_RESULT_DENIED, nothing reaching the wire.
//     Re-arming requires a NEW authorization: a second sanctioned forceArm() call
//     mints a fresh token and arms again. This guards the "one arm, one intent"
//     model end-to-end.
void ArmSendTest::testSameTokenSecondArmDeniedThenFreshMintArms()
{
    QVERIFY(_connectPlane());
    QVERIFY(!_vehicle->armed());

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);

    ArmingGate gate;
    _makeBlockedGate(gate, bridge);
    bridge.setArmingGate(&gate);

    // Mint the token ourselves exactly as TelemetryBridge::forceArm() does, so the
    // exact consumed value can be replayed to prove consumption.
    const quint32 token = gate.forceArm();
    QVERIFY2(token != 0, "forceArm() must mint a nonzero caller-scoped token");
    QVERIFY(gate.isOverrideActive());
    _vehicle->setArmOverrideToken(token);

    // First arm via the stock terminal (Vehicle::forceArm() == the
    // mainWindow.forceArmVehicleRequest handler body) presenting the token:
    // authorized and consumed.
    _mockLink->clearReceivedMavCommandCounts();
    _vehicle->forceArm();
    QVERIFY(_pumpUntil([](void* ctx) { return static_cast<Vehicle*>(ctx)->armed(); }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);

    // Same sanctioned caller, same window, SAME token value, second arm: the token
    // was consumed by the first command, so this must be denied (strict one-shot).
    _mockLink->clearReceivedMavCommandCounts();
    QSignalSpy denySpy(_vehicle, &Vehicle::mavCommandResult);
    _vehicle->setArmOverrideToken(token);
    _vehicle->forceArm();

    QTest::qWait(1500);   // give an (incorrect) reuse time to reach the link
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(qIsNaN(_mockLink->lastArmDisarmParam2()));

    bool sawDenied = false;
    const QList<QList<QVariant>> deny = denySpy;
    for (const QList<QVariant> &args : deny) {
        if (args.size() >= 4 && args.at(2).toInt() == MAV_CMD_COMPONENT_ARM_DISARM &&
            args.at(3).toInt() == MAV_RESULT_DENIED) {
            sawDenied = true;
            break;
        }
    }
    QVERIFY2(sawDenied, "same-token second arm inside the window was NOT denied "
                        "(token must be one-shot, not reusable-per-caller)");

    // Recovery path: a second SANCTIONED forceArm() mints a FRESH token and arms
    // again — one deliberate operator confirmation per arm.
    _mockLink->clearReceivedMavCommandCounts();
    const QString refusal = bridge.forceArm();
    QVERIFY2(refusal.isEmpty(), qPrintable(QStringLiteral("fresh force-arm refused on the ground: %1").arg(refusal)));
    QVERIFY(_pumpUntil([](void* ctx) {
        return static_cast<MockLink*>(ctx)->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);
    QVERIFY(_pumpUntil([](void* ctx) { return static_cast<Vehicle*>(ctx)->armed(); }, _vehicle, 10000));
    QVERIFY(_vehicle->armed());
}

UT_REGISTER_TEST(ArmSendTest)

#include "ArmSendTest.moc"