#include <QTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QCoreApplication>

#include "UnitTest.h"
#include "HardwareTestController.h"
#include "HardwareTestProfile.h"
#include "mocks/MockVehicle.h"
#include "mocks/MockTelemetryBridge.h"
#include "ArmingGate.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"
#include "utils/Config.h"
#include "managers/FlightSession.h"

class HardwareTestControllerTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        FlightSession::instance()->startTestingSession();
    }
    void cleanup() override {
        FlightSession::instance()->closeSession();
        UnitTest::cleanup();
    }

    void testInitialState();
    void testAbortSequence();
    void testServoSweepFinishedSignalExists();
    void testHardwareTestProfile();
    void testProfileWithTwoChannels();
    void testFixedWingTestLabelDefaultsToM3();
    void testFixedWingTestLabelWithoutVehicle();
    void testFixedWingMotorTestWaitsForArmBeforeSpin();
    void testFixedWingMotorTestArmsViaFunnelWith2989();
    void testStopAllLeavesExternallyArmedVehicleArmed();
    void testMotorTestRefusedWhileArmedExternally();
    void testCopterMotorTestSurvivesItsOwnArm();
    void testArmLeftAfterCopterMotorTestWarns();
    void testCopterPwmCeiling();
    void testCopterMotorTestHoldsForItsDuration();

private:
    // Unit-test singleton owner: FlightSession::instance() is null until a
    // session object exists (the app creates it via the QML singleton).
    FlightSession _session;
    bool _pumpUntil(bool (*cond)(void *ctx), void *ctx, int timeoutMs);
};

void HardwareTestControllerTest::testInitialState()
{
    HardwareTestController ctrl;
    QVERIFY(!ctrl.isRunning());
    QVERIFY(!ctrl.allPassed());
    QVERIFY(ctrl.lastErrorMessage().isEmpty());
    QCOMPARE(ctrl.stepProgress(), 0.0);
}

void HardwareTestControllerTest::testAbortSequence()
{
    HardwareTestController ctrl;
    // Without a bound vehicle the sweep cannot start; both start and abort
    // are guarded no-ops and must not crash.
    ctrl.runServoSweep();
    QVERIFY(!ctrl.isRunning());
    ctrl.abortSequence();
    QVERIFY(!ctrl.isRunning());
}

void HardwareTestControllerTest::testServoSweepFinishedSignalExists()
{
    // The GimbalTest.qml page connects to onServoSweepFinished; the signal
    // must be declared so that connection resolves and the UI leaves the
    // "running" state when a sweep completes.
    const QMetaObject *mo = &HardwareTestController::staticMetaObject;
    bool found = false;
    for (int i = 0; i < mo->methodCount(); ++i) {
        if (mo->method(i).methodType() == QMetaMethod::Signal &&
                mo->method(i).name() == QLatin1String("servoSweepFinished")) {
            found = true;
            QCOMPARE(mo->method(i).parameterCount(), 2);
            break;
        }
    }
    QVERIFY(found);
}

void HardwareTestControllerTest::testHardwareTestProfile()
{
    HardwareTestProfile profile;
    QVERIFY(profile.profileName.isEmpty());
    QVERIFY(profile.steps.isEmpty());
}

void HardwareTestControllerTest::testProfileWithTwoChannels()
{
    HardwareTestProfile profile;
    profile.profileName = QStringLiteral("test");
    TestStep stepA;
    stepA.name = QStringLiteral("Channel 1");
    stepA.servoInstance = 1;
    stepA.targetPwm = 1500;
    profile.steps.append(stepA);
    TestStep stepB;
    stepB.name = QStringLiteral("Channel 2");
    stepB.servoInstance = 2;
    stepB.targetPwm = 1800;
    profile.steps.append(stepB);
    QCOMPARE(profile.steps.size(), 2);
    QVERIFY(!profile.isValid());
}

void HardwareTestControllerTest::testFixedWingTestLabelDefaultsToM3()
{
    HardwareTestController ctrl;
    // Without a vehicle the controller is not fixed-wing, so the label falls
    // back to the generic "M1" (matches the new unified DO_MOTOR_TEST path).
    QCOMPARE(ctrl.fixedWingTestLabel(), QStringLiteral("M1"));
}

void HardwareTestControllerTest::testFixedWingTestLabelWithoutVehicle()
{
    HardwareTestController ctrl;
    // Motor output channel must be safe without a bound vehicle.
    QCOMPARE(ctrl.motorOutputChannel(1), 1);
}

bool HardwareTestControllerTest::_pumpUntil(bool (*cond)(void *ctx), void *ctx, int timeoutMs)
{
    const quint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (cond(ctx)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(50);
    }
    return cond(ctx);
}

// The fixed-wing motor test must wait for the autopilot to CONFIRM the
// arm (phase 1) before driving the throttle RC override — the ESC must never
// spin with zero confirmation the vehicle actually armed.  Uses a real
// ArduPlane mock Vehicle so the whole arm → spin → spin-down → evaluate
// sequence runs end-to-end; a stuck phase-1 wait or a leaked timer would hang
// the loop below and fail the test.
void HardwareTestControllerTest::testFixedWingMotorTestWaitsForArmBeforeSpin()
{
    _mockLink = MockLink::startAPMArduPlaneMockLink(false);
    QVERIFY(_mockLink);
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    QVERIFY(spyVehicle.wait(10000));
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);
    QSignalSpy spyPlan(_vehicle, &Vehicle::initialConnectComplete);
    QVERIFY(spyPlan.wait(30000));

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);

    // Route the FW auto-arm through the single funnel. The gate is left closed
    // (no override pre-opened) so the funnel must open the bounded override
    // itself and send exactly ONE arm command with the canonical 2989 param2 —
    // never the legacy 21196, and never a second parallel command.
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);
    bridge.setArmingGate(&gate);
    ctrl.setTelemetryBridge(&bridge);

    // Motor count must resolve (HEARTBEAT fallback → 1 for a plane) before a test can start.
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));

    // A plane's motor test PWM goes up to 1800 µs (the copter ceiling is lower).
    ctrl.setMotorPwm(1, 2000);
    QCOMPARE(ctrl.motorPwmValues().value(0).toInt(), int(HardwareTestController::kFwMaxPwmUs));
    ctrl.setMotorPwm(1, HardwareTestController::kFwDefaultPwmUs);

    _mockLink->clearReceivedMavCommandCounts();

    ctrl.testMotor(1);

    // Exactly one arm on the wire, with the funnel's param2.
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<MockLink *>(ctx)->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 1.0f);
    QCOMPARE(_mockLink->lastArmDisarmParam2(), kForceArmParam2);
    QVERIFY(_mockLink->lastArmDisarmParam2() != kForceDisarmParam2);
    // The funnel opened the bounded override rather than the controller sending blind.
    QVERIFY(gate.isOverrideActive());

    // Track whether the mock vehicle reported armed during the run (the mock
    // ACKs the force-arm, which is exactly the confirmation phase 1 waits for).
    bool sawArmed = false;
    const quint64 t1 = QDateTime::currentMSecsSinceEpoch();
    while (ctrl.activeMotor() != -1 && QDateTime::currentMSecsSinceEpoch() - t1 < 30000) {
        if (_vehicle->armed()) sawArmed = true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(100);
    }
    QCOMPARE(ctrl.activeMotor(), -1);
    QVERIFY2(sawArmed, "vehicle never reported armed during the FW test — phase-1 arm wait never satisfied or arm never ACKed");

    // Terminal verdict: a settled state, never still Testing.
    const int st = ctrl.motorStatus(1);
    QVERIFY2(st == HardwareTestController::Pass || st == HardwareTestController::Fail
                  || st == HardwareTestController::Cooldown,
             qPrintable(QStringLiteral("motor state stuck at %1").arg(st)));

    // The spin-down path disarms; nothing may be left armed after the test.
    QVERIFY(_pumpUntil([](void *ctx) {
        Vehicle *v = static_cast<Vehicle *>(ctx);
        return !v->armed();
    }, _vehicle, 10000));
    // ...and it is a PLAIN disarm from BenchDisarm, never the forced 21196.
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 0.0f);
    QCOMPARE(_mockLink->lastArmDisarmParam2(), 0.0f);
}

// Phase 3 funnel convergence, isolated from the full motor run: the FW auto-arm
// must go through TelemetryBridge::forceArm() — which opens the bounded gate
// override and sends ONE command with the canonical 2989 param2 — instead of a
// blind sendMavCommand() with the legacy 21196 magic.
void HardwareTestControllerTest::testFixedWingMotorTestArmsViaFunnelWith2989()
{
    _mockLink = MockLink::startAPMArduPlaneMockLink(false);
    QVERIFY(_mockLink);
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    QVERIFY(spyVehicle.wait(10000));
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);
    QSignalSpy spyPlan(_vehicle, &Vehicle::initialConnectComplete);
    QVERIFY(spyPlan.wait(30000));

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setVehicle(_vehicle);
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);
    bridge.setArmingGate(&gate);
    ctrl.setTelemetryBridge(&bridge);

    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));

    _mockLink->clearReceivedMavCommandCounts();

    ctrl.testMotor(1);

    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<MockLink *>(ctx)->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM) >= 1;
    }, _mockLink, 10000));

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1);
    QCOMPARE(_mockLink->lastArmDisarmParam1(), 1.0f);
    QCOMPARE(_mockLink->lastArmDisarmParam2(), kForceArmParam2);
    QVERIFY2(_mockLink->lastArmDisarmParam2() != kForceDisarmParam2,
             "FW motor test still sends the legacy 21196 force-arm magic instead of the funnel's 2989");
    QVERIFY2(gate.isOverrideActive(), "funnel did not open the bounded gate override");

    // Airborne force-arm is refused by the funnel, so the same controller path
    // must not arm: stop the run and put the vehicle back on the ground.
    bridge.setGroundSpeed(static_cast<double>(kForceArmMaxGroundSpeedMs) + 5.0);
    const QString refusal = bridge.forceArm(QStringLiteral("TestPilot"),
                                            QStringLiteral("Airborne refusal test"));
    QVERIFY2(!refusal.isEmpty(), "funnel force-armed an airborne vehicle");
    QTest::qWait(400);
}

// STOP ALL stops outputs but must never disarm a vehicle this test did
// not arm — it may be flying. (An earlier version asserted the
// opposite: STOP ALL used to force-disarm any armed vehicle.)
void HardwareTestControllerTest::testStopAllLeavesExternallyArmedVehicleArmed()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);

    // Armed by someone else (RC / another GCS): a plain arm on the link.
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 1.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->isArmed();
    }, &ctrl, 10000));

    _mockLink->clearReceivedMavCommandCounts();
    ctrl.stopAll();
    QTest::qWait(1500);

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY2(_vehicle->armed(), "STOP ALL disarmed a vehicle the test did not arm");

    // Clean up: the test harness disarms it.
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 0.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) { return !static_cast<Vehicle *>(ctx)->armed(); }, _vehicle, 10000));
}

// A motor test on an already-armed vehicle is refused with a reason; the
// controller must not disarm it (it used to force-disarm first).
void HardwareTestControllerTest::testMotorTestRefusedWhileArmedExternally()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));

    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 1.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->isArmed();
    }, &ctrl, 10000));
    QVERIFY(!ctrl.testBlockedReason().isEmpty());

    _mockLink->clearReceivedMavCommandCounts();
    ctrl.testMotor(1);
    QTest::qWait(1500);

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST), 0);
    QCOMPARE(ctrl.activeMotor(), -1);
    QVERIFY(!ctrl.lastErrorMessage().isEmpty());
    QVERIFY(_vehicle->armed());

    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 0.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) { return !static_cast<Vehicle *>(ctx)->armed(); }, _vehicle, 10000));
}

// ArduCopter reports itself armed while it runs MAV_CMD_DO_MOTOR_TEST. That
// arm belongs to the test: the test must run its full duration, not stop
// itself as if a pilot had armed. While it runs it counts as a bench test.
void HardwareTestControllerTest::testCopterMotorTestSurvivesItsOwnArm()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));
    QVERIFY(!ctrl.isFixedWing());
    QVERIFY(!ctrl.testInProgress());

    ctrl.setDurationSec(2);
    _mockLink->clearReceivedMavCommandCounts();
    ctrl.testMotor(1);
    QCOMPARE(ctrl.activeMotor(), 1);
    QVERIFY(ctrl.testInProgress());
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<MockLink *>(ctx)->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST) >= 1;
    }, _mockLink, 5000));

    // The copter arms its outputs for the test (simulated with a plain arm).
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 1.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->isArmed();
    }, &ctrl, 10000));
    QTest::qWait(300);
    QCOMPARE(ctrl.activeMotor(), 1);   // not cancelled
    QCOMPARE(ctrl.motorStatus(1), int(HardwareTestController::Testing));
    QVERIFY(ctrl.testInProgress());

    // The copter ends the test and disarms before the window closes.
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 0.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) { return !static_cast<Vehicle *>(ctx)->armed(); }, _vehicle, 10000));

    // The window (2 s + grace) closes with a result, not a cancellation.
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->activeMotor() == -1;
    }, &ctrl, 8000));
    const int st = ctrl.motorStatus(1);
    QVERIFY2(st == HardwareTestController::Cooldown || st == HardwareTestController::Pass
                 || st == HardwareTestController::Fail,
             qPrintable(QStringLiteral("state %1").arg(st)));
    QVERIFY2(ctrl.lastErrorMessage().isEmpty(), qPrintable(ctrl.lastErrorMessage()));
    QVERIFY(!ctrl.testInProgress());
}

// If the copter is still armed when the test window closes, the arm was not the
// test's (RC or another GCS): say so. Nothing is disarmed.
void HardwareTestControllerTest::testArmLeftAfterCopterMotorTestWarns()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));

    ctrl.setDurationSec(1);
    ctrl.testMotor(1);
    QCOMPARE(ctrl.activeMotor(), 1);
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 1.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->isArmed();
    }, &ctrl, 10000));

    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->activeMotor() == -1;
    }, &ctrl, 8000));
    QVERIFY2(ctrl.lastErrorMessage().contains(QStringLiteral("still armed")), qPrintable(ctrl.lastErrorMessage()));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(_vehicle->armed());

    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 0.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) { return !static_cast<Vehicle *>(ctx)->armed(); }, _vehicle, 10000));
}

// A copter motor test goes up to 1300 µs; higher values clamp.
void HardwareTestControllerTest::testCopterPwmCeiling()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));
    QVERIFY(!ctrl.isFixedWing());

    ctrl.setMotorPwm(1, 1250);
    QCOMPARE(ctrl.motorPwmValues().value(0).toInt(), 1250);
    ctrl.setMotorPwm(1, 1800);
    QCOMPARE(ctrl.motorPwmValues().value(0).toInt(), 1300);
    QCOMPARE(ctrl.copterMaxPwmUs(), 1300);
}

// The duration slider sets how long a copter motor test runs: the test stays
// active for that long (DO_MOTOR_TEST's timeout is the same durationSec), and
// only then gives its result.
void HardwareTestControllerTest::testCopterMotorTestHoldsForItsDuration()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));
    QVERIFY(!ctrl.isFixedWing());

    ctrl.setDurationSec(4);
    QCOMPARE(ctrl.durationSec(), 4);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    ctrl.testMotor(1);
    QCOMPARE(ctrl.activeMotor(), 1);

    // Still running 3.5 s in.
    while (QDateTime::currentMSecsSinceEpoch() - t0 < 3500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(50);
    }
    QCOMPARE(ctrl.activeMotor(), 1);
    QCOMPARE(ctrl.motorStatus(1), int(HardwareTestController::Testing));

    // Ends after the duration plus the result grace.
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->activeMotor() == -1;
    }, &ctrl, 4000));
    QVERIFY(QDateTime::currentMSecsSinceEpoch() - t0 >= 4000);
}

UT_REGISTER_TEST(HardwareTestControllerTest)

#include "HardwareTestControllerTest.moc"
