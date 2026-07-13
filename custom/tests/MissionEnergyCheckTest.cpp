#include <QTest>

#include "UnitTest.h"
#include "MissionEnergyCheck.h"
#include "PowerModel.h"
#include "WaypointMath.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"

class MissionEnergyCheckTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testHaversineDistance();
    void testEnergyCheckCreation();
    void testCheckWithoutTelemetry();
    void testCheckWithTelemetryNoMission();
    void testEnergyPass();
    void testEnergyFail();
    void testEnergyWarning();
    void testMOT_THST_HOVERScalesPower();
};

static const double kLat0 = 47.0;
static const double kLon0 = 8.0;
static const double kLat1 = 47.01;
static const double kLon1 = 8.01;

void MissionEnergyCheckTest::testHaversineDistance()
{
    QGeoCoordinate from(kLat0, kLon0);
    QGeoCoordinate to(kLat1, kLon1);

    double dist = WaypointMath::distanceBetweenCoordinates(from, to);
    QVERIFY(dist > 0.0);
    QVERIFY(dist < 2000.0);

    double selfDist = WaypointMath::distanceBetweenCoordinates(from, from);
    QCOMPARE(selfDist, 0.0);
}

void MissionEnergyCheckTest::testEnergyCheckCreation()
{
    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    QCOMPARE(check.id(), QStringLiteral("safety.mission.energy"));
    QVERIFY(check.mandatory() == false);
    QVERIFY(check.isAuto());
}

void MissionEnergyCheckTest::testCheckWithoutTelemetry()
{
    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Skipped);
}

void MissionEnergyCheckTest::testCheckWithTelemetryNoMission()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.m_telemetry = &bridge;
    check.evaluate();

    QVERIFY(check.status() == CheckStatus::Skipped ||
            check.status() == CheckStatus::Warning);
}

void MissionEnergyCheckTest::testEnergyPass()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setBatteryVoltage(25.2);
    bridge.setMissionCount(5);
    bridge.setMissionTotalDistance(1000.0);

    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.m_telemetry = &bridge;
    // 15000 mAh 6S = 333 Wh. Required ~136 Wh (40.8% < 50%) → PASS
    check.setTestValue(QStringLiteral("BATT_CAPACITY"), 15000.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

void MissionEnergyCheckTest::testEnergyFail()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setBatteryVoltage(25.2);
    bridge.setMissionCount(5);
    bridge.setMissionTotalDistance(1000.0);

    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.m_telemetry = &bridge;
    // 5000 mAh 6S = 111 Wh. Required ~136 Wh > safetyWh 66.6 → FAIL
    check.setTestValue(QStringLiteral("BATT_CAPACITY"), 5000.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
}

void MissionEnergyCheckTest::testEnergyWarning()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setBatteryVoltage(25.2);
    bridge.setMissionCount(5);
    bridge.setMissionTotalDistance(1000.0);

    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.m_telemetry = &bridge;
    // 11000 mAh 6S = 244.2 Wh. Required ~136 = 55.7% > 50% but < 60% safety → WARN
    check.setTestValue(QStringLiteral("BATT_CAPACITY"), 11000.0);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
}

void MissionEnergyCheckTest::testMOT_THST_HOVERScalesPower()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setBatteryVoltage(25.2);
    bridge.setMissionCount(5);
    bridge.setMissionTotalDistance(1000.0);

    PowerModel powerModel;
    MissionEnergyCheck check(&powerModel, 0.6);
    check.m_telemetry = &bridge;
    // 5000 mAh 6S = 111 Wh. Required with MOT_THST_HOVER=0.20 (higher hover power)
    // hoverWhPerMin = 8 * (0.20/0.15) = 10.67 → hoverWh = 21.33 → total = 141.33
    check.setTestValue(QStringLiteral("BATT_CAPACITY"), 5000.0);
    check.setTestValue(QStringLiteral("MOT_THST_HOVER"), 0.20);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
    QVERIFY(check.message().contains(QStringLiteral("MOT_THST_HOVER=0.200")));
}

UT_REGISTER_TEST(MissionEnergyCheckTest)

#include "MissionEnergyCheckTest.moc"
