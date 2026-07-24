#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "mocks/MockTelemetryBridge.h"

class TelemetryBridgeTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testDefaultValues();
    void testNewPropertiesDefaultValues();
    void testNewPropertySetters();
    void testParameterCache();
    void testHasParameter();
};

void TelemetryBridgeTest::testDefaultValues()
{
    MockTelemetryBridge bridge;
    QVERIFY(!bridge.isConnected());
    QCOMPARE(bridge.connectionQuality(), 0);
    QCOMPARE(bridge.batteryVoltage(), 0.0);
    QCOMPARE(bridge.gpsSatellites(), 0);
    QVERIFY(!bridge.armed());
    QCOMPARE(bridge.rcRssi(), 0);
}

void TelemetryBridgeTest::testNewPropertiesDefaultValues()
{
    MockTelemetryBridge bridge;
    QCOMPARE(bridge.vehicleType(), QString());
    QCOMPARE(bridge.connectionStatus(), QString());
    QCOMPARE(bridge.connectionUrl(), QString());
    QCOMPARE(bridge.autopilotType(), QString());
    QCOMPARE(bridge.gpsFixTypeString(), QString());
    QCOMPARE(bridge.gpsDataQuality(), 2);
    QCOMPARE(bridge.gpsStatusString(), QString());
    QCOMPARE(bridge.batteryDataQuality(), 2);
    QCOMPARE(bridge.batteryStatusString(), QString());
    QCOMPARE(bridge.servoOutputsString(), QString());
    QCOMPARE(bridge.gimbalPitch(), 0.0);
    QCOMPARE(bridge.gimbalRoll(), 0.0);
    QCOMPARE(bridge.gimbalYaw(), 0.0);
    QVERIFY(!bridge.gimbalCalibrating());
    QCOMPARE(bridge.ekfAirspeedVariance(), 0.0);
    QCOMPARE(bridge.ekfStatus(), QString());
    QCOMPARE(bridge.flightTime(), 0.0);
    QCOMPARE(bridge.lastLogTimestamp(), QString());
    QCOMPARE(bridge.preArmSeverity(), QString());
    QCOMPARE(bridge.imuDataQuality(), 3);
    QCOMPARE(bridge.compassDataQuality(), 3);
    QCOMPARE(bridge.rcDataQuality(), 2);
    QVERIFY(!bridge.hardwareSetupRequired());
}

void TelemetryBridgeTest::testNewPropertySetters()
{
    MockTelemetryBridge bridge;

    bridge.setVehicleType("Quad");
    QCOMPARE(bridge.vehicleType(), QStringLiteral("Quad"));

    bridge.setGpsFixTypeString("3D Fix");
    QCOMPARE(bridge.gpsFixTypeString(), QStringLiteral("3D Fix"));

    bridge.setGpsDataQuality(1);
    QCOMPARE(bridge.gpsDataQuality(), 1);

    bridge.setBatteryDataQuality(0);
    QCOMPARE(bridge.batteryDataQuality(), 0);

    bridge.setServoOutputsString("1500,1500,1500,1500");
    QCOMPARE(bridge.servoOutputsString(), QStringLiteral("1500,1500,1500,1500"));

    bridge.setGimbalAttitude(10.0, 5.0, -3.0);
    QCOMPARE(bridge.gimbalPitch(), 10.0);
    QCOMPARE(bridge.gimbalRoll(), 5.0);
    QCOMPARE(bridge.gimbalYaw(), -3.0);

    bridge.setGimbalCalibrating(true);
    QVERIFY(bridge.gimbalCalibrating());

    bridge.setEkfStatus("OK");
    QCOMPARE(bridge.ekfStatus(), QStringLiteral("OK"));

    bridge.setFlightTime(123.456);
    QCOMPARE(bridge.flightTime(), 123.456);

    bridge.setLastLogTimestamp("2026-01-15T10:30:00Z");
    QCOMPARE(bridge.lastLogTimestamp(), QStringLiteral("2026-01-15T10:30:00Z"));

    bridge.setPreArmSeverity("warning");
    QCOMPARE(bridge.preArmSeverity(), QStringLiteral("warning"));

    bridge.setImuDataQuality(1);
    QCOMPARE(bridge.imuDataQuality(), 1);

    bridge.setCompassDataQuality(0);
    QCOMPARE(bridge.compassDataQuality(), 0);

    bridge.setRcDataQuality(1);
    QCOMPARE(bridge.rcDataQuality(), 1);

    bridge.setHardwareSetupRequired(true);
    QVERIFY(bridge.hardwareSetupRequired());
}

void TelemetryBridgeTest::testParameterCache()
{
    MockTelemetryBridge bridge;
    QVERIFY(!bridge.hasParameter("RTL_ALT"));

    // setParameterValue should store in cache
    bridge.setParameterValue("RTL_ALT", 100.0f);
    QVERIFY(bridge.hasParameter("RTL_ALT"));
    QCOMPARE(bridge.parameterValue("RTL_ALT"), 100.0f);
    QCOMPARE(bridge.parameterValue("NONEXISTENT", 42.0f), 42.0f);
}

void TelemetryBridgeTest::testHasParameter()
{
    MockTelemetryBridge bridge;
    QVERIFY(!bridge.hasParameter("any_param"));

    bridge.setParameterValue("test_key", 1.0f);
    QVERIFY(bridge.hasParameter("test_key"));
    QVERIFY(!bridge.hasParameter("other_key"));
}

UT_REGISTER_TEST(TelemetryBridgeTest)

#include "TelemetryBridgeTest.moc"
