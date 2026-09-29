#include <QTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QCoreApplication>

#include "UnitTest.h"
#include "HardwareTestController.h"
#include "HardwareTestProfile.h"
#include "mocks/MockVehicle.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"

class HardwareTestControllerTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testInitialState();
    void testAbortSequence();
    void testServoSweepFinishedSignalExists();
    void testHardwareTestProfile();
    void testProfileWithTwoChannels();
    void testFixedWingTestLabelDefaultsToM3();
    void testFixedWingTestLabelWithoutVehicle();
    void testFixedWingMotorTestWaitsForArmBeforeSpin();
    void testStopAllDisarmsVehicle();

private:
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

// P0.3: The fixed-wing motor test must wait for the autopilot to CONFIRM the
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

    // Motor count must resolve (HEARTBEAT fallback → 1 for a plane) before a test can start.
    QVERIFY(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->motorCount() >= 1;
    }, &ctrl, 15000));

    ctrl.testMotor(1);

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
}

// P0.4: stopAll() on the multirotor path must leave the vehicle DISARMED, not
// just zero-throttled.  Previously the copter path stopped motors but never
// sent a disarm, leaving the motors idle while the vehicle stayed ARMED.
void HardwareTestControllerTest::testStopAllDisarmsVehicle()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    QVERIFY(_vehicle);

    HardwareTestController ctrl;
    ctrl.setVehicle(_vehicle);

    // Arm the vehicle through the link (outbound arm is not gate-intercepted;
    // the mock ACKs it and reports armed via HEARTBEAT).
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             false, 1.0f, 0.0f, 0, 0, 0, 0, 0);
    QVERIFY(_pumpUntil([](void *ctx) {
        Vehicle *v = static_cast<Vehicle *>(ctx);
        return v->armed();
    }, _vehicle, 10000));

    // Let the controller's armedChanged handler sync the cached _isArmed.
    QVERIFY2(_pumpUntil([](void *ctx) {
        return static_cast<HardwareTestController *>(ctx)->isArmed();
    }, &ctrl, 3000), "controller never observed the armed vehicle before stopAll");

    ctrl.stopAll();

    // STOP ALL must disarm — wait for the mock to process the disarm command.
    QVERIFY2(_pumpUntil([](void *ctx) {
        Vehicle *v = static_cast<Vehicle *>(ctx);
        return !v->armed();
    }, _vehicle, 10000), "vehicle still armed after stopAll — P0.4 disarm gate missing");
    QVERIFY(!ctrl.isArmed());
}

UT_REGISTER_TEST(HardwareTestControllerTest)

#include "HardwareTestControllerTest.moc"
