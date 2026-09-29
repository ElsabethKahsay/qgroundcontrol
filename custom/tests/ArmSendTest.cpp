/**
 * @file ArmSendTest.cpp
 * @brief Wire-level arm send tests: verify TelemetryBridge::arm()/forceArm()
 *        actually send (or suppress) MAV_CMD_COMPONENT_ARM_DISARM over a real
 *        MockLink, using MockLink's received-command counter as the oracle.
 *
 * Three scenarios:
 *   (a) failing critical check (gate closed) -> arm() sends nothing, vehicle stays disarmed
 *   (b) forceArm() then arm()               -> exactly one arm command, vehicle arms
 *   (c) override expires (setOverrideDuration) -> arm() blocked again, no new command
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
//     The stock path does NOT consult the ArmingGate: it sends the arm command
//     to the wire and arms the vehicle regardless of the blocked gate. This is
//     the live bypass captured in build/evidence-live-bypass.txt, so this test
//     is marked QEXPECT_FAIL(...Continue): it asserts the desired gated
//     behavior (zero arm commands, disarmed vehicle) and currently fails.
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

    QEXPECT_FAIL("", "live bypass: stock arm path ungated, see evidence-live-bypass.txt", Continue);
    QVERIFY2(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) == 0 &&
             !_vehicle->armed(),
             qPrintable(QStringLiteral("armCount=%1 armed=%2 usedByUnexpectedGate")
                            .arg(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM))
                            .arg(_vehicle->armed() ? "true" : "false")));
}

UT_REGISTER_TEST(ArmSendTest)

#include "ArmSendTest.moc"