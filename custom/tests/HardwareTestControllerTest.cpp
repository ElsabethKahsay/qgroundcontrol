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
    void testHardwareTestProfile();
    void testProfileWithTwoChannels();
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
    MockVehicle vehicle;
    vehicle.setConnected(true);
    ctrl.setVehicle(reinterpret_cast<Vehicle *>(&vehicle));

    ctrl.runMotorTest();
    ctrl.abortSequence();
    QVERIFY(!ctrl.isRunning());
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

UT_REGISTER_TEST(HardwareTestControllerTest)

#include "HardwareTestControllerTest.moc"
