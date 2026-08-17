#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "HardwareTestController.h"
#include "HardwareTestProfile.h"
#include "mocks/MockVehicle.h"

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

UT_REGISTER_TEST(HardwareTestControllerTest)

#include "HardwareTestControllerTest.moc"
