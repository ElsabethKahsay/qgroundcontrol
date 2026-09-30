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
 *   (e) no interceptor installed (no-op)    -> stock setArmed arm is untouched: exactly one
 *                                              command with param1=1, vehicle arms
 *   (f) disarm(false) path does not arm     -> VehicleProfileManager::disarmVehicle(false)
 *                                              sends param1=0 (disarm), never an arm payload
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

private:
    bool _pumpUntil(bool (*cond)(void* ctx), void* ctx, int timeoutMs);
    Vehicle* _connectPlane();
    void _makeBlockedGate(ArmingGate &gate, MockTelemetryBridge &bridge);
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

UT_REGISTER_TEST(ArmSendTest)

#include "ArmSendTest.moc"