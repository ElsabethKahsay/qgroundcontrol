#include <QTest>
#include <QSignalSpy>
#include <QtMath>
#include <QMap>
#include <cmath>

#include "UnitTest.h"
#include "Vehicle/Vehicle.h"
#include "mocks/MockTelemetryBridge.h"
#include "core/TrainerSwitchTracker.h"

class TelemetryBridgeTest : public UnitTest {
    Q_OBJECT

private slots:
    void testOnlyAutopilotHeartbeatCounts();
    void testConnectionQualityDecaysWithHeartbeatGap();
    void testGimbalQuaternionDecodes();
    void testGimbalEarthFrameYawIsMadeRelative();
    void testGimbalMountMessagesDecode();
    void testTrainerSwitchTracker();
    void testTrainerChannelDetector();
    void testAskForHomeNeedsBothCoordinates();
    void testPartialHomeMessageKeepsAsking();
    void testPartialHomeIsNotLostOnVehicleSwitch();
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

// Only the vehicle's own autopilot component counts as link
// liveness; a companion computer (191) or gimbal (154) heartbeat does not.
void TelemetryBridgeTest::testOnlyAutopilotHeartbeatCounts()
{
    QVERIFY(TelemetryBridge::isAutopilotMessage(1, 1, 1, 1));
    QVERIFY(!TelemetryBridge::isAutopilotMessage(1, 191, 1, 1));   // companion
    QVERIFY(!TelemetryBridge::isAutopilotMessage(1, 154, 1, 1));   // gimbal
    QVERIFY(!TelemetryBridge::isAutopilotMessage(2, 1, 1, 1));     // another vehicle's autopilot
}

// Quality falls to 0 within the 10 s staleness limit when
// autopilot heartbeats stop, instead of freezing at the last value.
void TelemetryBridgeTest::testConnectionQualityDecaysWithHeartbeatGap()
{
    QCOMPARE(TelemetryBridge::connectionQualityForGap(0, 0.0), 100);
    QCOMPARE(TelemetryBridge::connectionQualityForGap(1200, 0.0), 100);
    QVERIFY(TelemetryBridge::connectionQualityForGap(3000, 0.0) < 100);
    QCOMPARE(TelemetryBridge::connectionQualityForGap(6000, 0.0), 0);
    QCOMPARE(TelemetryBridge::connectionQualityForGap(10000, 0.0), 0);
    QCOMPARE(TelemetryBridge::connectionQualityForGap(0, 30.0), 70);   // drop-rate cap
}

// A quaternion for pitch -30, roll 10, yaw 25 (vehicle frame,
// ZYX order) decodes back to those angles.
static void quatFromEulerDeg(double rollDeg, double pitchDeg, double yawDeg, float q[4])
{
    const double cr = std::cos(qDegreesToRadians(rollDeg) / 2), sr = std::sin(qDegreesToRadians(rollDeg) / 2);
    const double cp = std::cos(qDegreesToRadians(pitchDeg) / 2), sp = std::sin(qDegreesToRadians(pitchDeg) / 2);
    const double cy = std::cos(qDegreesToRadians(yawDeg) / 2), sy = std::sin(qDegreesToRadians(yawDeg) / 2);
    q[0] = float(cr * cp * cy + sr * sp * sy);
    q[1] = float(sr * cp * cy - cr * sp * sy);
    q[2] = float(cr * sp * cy + sr * cp * sy);
    q[3] = float(cr * cp * sy - sr * sp * cy);
}

static mavlink_message_t gimbalStatus(const float q[4], uint16_t flags, float deltaYaw)
{
    mavlink_message_t msg;
    mavlink_msg_gimbal_device_attitude_status_pack(1, MAV_COMP_ID_GIMBAL, &msg, 0, 0, 0, flags, q,
                                                   0.f, 0.f, 0.f, 0, deltaYaw, NAN, 1);
    return msg;
}

void TelemetryBridgeTest::testGimbalQuaternionDecodes()
{
    float q[4];
    quatFromEulerDeg(10.0, -30.0, 25.0, q);
    double p = 0, r = 0, y = 0;
    QVERIFY(TelemetryBridge::decodeGimbalAttitude(gimbalStatus(q, GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME, NAN), 0.0, p, r, y));
    QVERIFY2(qAbs(p + 30.0) < 0.01, qPrintable(QString::number(p)));
    QVERIFY2(qAbs(r - 10.0) < 0.01, qPrintable(QString::number(r)));
    QVERIFY2(qAbs(y - 25.0) < 0.01, qPrintable(QString::number(y)));

    mavlink_message_t hb;
    mavlink_msg_heartbeat_pack(1, 1, &hb, MAV_TYPE_FIXED_WING, MAV_AUTOPILOT_ARDUPILOTMEGA, 0, 0, 0);
    QVERIFY(!TelemetryBridge::decodeGimbalAttitude(hb, 0.0, p, r, y));
}

// An earth-frame yaw (gimbal at 100 deg true, vehicle heading 80 deg) is 20 deg
// right of the nose — from delta_yaw, or from the vehicle yaw when it is NaN.
void TelemetryBridgeTest::testGimbalEarthFrameYawIsMadeRelative()
{
    float q[4];
    quatFromEulerDeg(0.0, 0.0, 100.0, q);
    double p = 0, r = 0, y = 0;
    QVERIFY(TelemetryBridge::decodeGimbalAttitude(
        gimbalStatus(q, GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME, float(qDegreesToRadians(80.0))), 0.0, p, r, y));
    QVERIFY2(qAbs(y - 20.0) < 0.01, qPrintable(QString::number(y)));
    QVERIFY(TelemetryBridge::decodeGimbalAttitude(
        gimbalStatus(q, GIMBAL_DEVICE_FLAGS_YAW_LOCK, NAN), qDegreesToRadians(80.0), p, r, y));
    QVERIFY2(qAbs(y - 20.0) < 0.01, qPrintable(QString::number(y)));
}

void TelemetryBridgeTest::testGimbalMountMessagesDecode()
{
    mavlink_message_t msg;
    double p = 0, r = 0, y = 0;
    mavlink_msg_mount_status_pack(1, 1, &msg, 0, 0, -3000, 500, 35000, 2);   // centidegrees
    QVERIFY(TelemetryBridge::decodeGimbalAttitude(msg, 0.0, p, r, y));
    QCOMPARE(p, -30.0);
    QCOMPARE(r, 5.0);
    QCOMPARE(y, -10.0);   // 350 deg wraps to -10
    mavlink_msg_mount_orientation_pack(1, 1, &msg, 0, 2.0f, -15.0f, 40.0f, 0.0f);
    QVERIFY(TelemetryBridge::decodeGimbalAttitude(msg, 0.0, p, r, y));
    QCOMPARE(p, -15.0);
    QCOMPARE(r, 2.0);
    QCOMPARE(y, 40.0);
}

// Trainer handover (operator convention): SH high (> 1600 us) = instructor /
// master flying; SH low (< 1400 us) = trainee; in between = no change; no
// valid value = unmonitored. Verified once both positions were seen.
void TelemetryBridgeTest::testTrainerSwitchTracker()
{
    auto ch = [](int ch9) {
        QVariantList v;
        for (int i = 0; i < 9; ++i)
            v << (i == 8 ? ch9 : 1500);
        return v;
    };
    TrainerSwitchTracker t;
    t.setChannel(9);
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Unmonitored);
    QVERIFY(!t.update(ch(1500)));                       // middle at start: not decided yet
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Unmonitored);
    QVERIFY(t.update(ch(987)));                         // SH low: trainee
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Student);
    QVERIFY(!t.verified());
    QVERIFY(t.update(ch(2011)));                        // SH high: instructor
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Instructor);
    QVERIFY(t.verified());
    QVERIFY(!t.update(ch(1500)));                       // in between: no flicker
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Instructor);
    // Connected with SH already high: instructor straight away (no baseline guess).
    t.reset();
    QVERIFY(t.update(ch(2011)));
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Instructor);

    t.setHighIsInstructor(false);                       // radio wired the other way
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Student);
    t.update(ch(987));
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Instructor);

    QVERIFY(t.update(QVariantList{1500, 1500, 1500, 1500}));   // channel missing
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Unmonitored);
    t.update(ch(65535));                                // unused channel value
    QCOMPARE(t.control(), TrainerSwitchTracker::Control::Unmonitored);
    QCOMPARE(TrainerSwitchTracker::name(TrainerSwitchTracker::Control::Student), QStringLiteral("student"));
}

// Detect: the one switch channel flicked low/high a few times wins; sticks,
// one-off moves and the flight-mode channel are not offered.
void TelemetryBridgeTest::testTrainerChannelDetector()
{
    // 16 channels: sticks centred, CH8 mode switch at 987, the rest unused (987).
    auto frame = [](QMap<int, int> set) {
        QVariantList v;
        for (int ch = 1; ch <= 16; ++ch)
            v << set.value(ch, ch <= 4 ? 1500 : 987);
        return v;
    };
    using O = TrainerChannelDetector::Outcome;
    TrainerChannelDetector d;
    int ch = -1;
    QList<int> several;

    // SH on CH9, flicked twice; a stick moves a lot meanwhile (ignored).
    d.start(8);
    for (int us : {987, 2011, 987, 2011, 987})
        d.feed(frame({{9, us}, {1, us}}));
    QCOMPARE(d.finish(&ch), O::Found);
    QCOMPARE(ch, 9);
    QVERIFY(d.clearEarly());                         // 4 transitions on one channel

    // Only one flick: not enough.
    d.start(8);
    d.feed(frame({{9, 987}}));
    d.feed(frame({{9, 2011}}));
    QCOMPARE(d.finish(&ch), O::NoMovement);

    // Two switches flicked together.
    d.start(8);
    for (int us : {987, 2011, 987, 2011})
        d.feed(frame({{9, us}, {11, us}}));
    QCOMPARE(d.finish(&ch, &several), O::Several);
    QCOMPARE(several, (QList<int>{9, 11}));

    // Only the flight-mode channel moved.
    d.start(8);
    for (int us : {987, 1500, 2011, 987, 2011})
        d.feed(frame({{8, us}}));
    QCOMPARE(d.finish(&ch), O::OnlyModeChannel);

    // No RC at all (0 on every channel).
    d.start(8);
    d.feed(QVariantList(16, 0));
    QCOMPARE(d.finish(&ch), O::NoRc);
}

// Home is known only when both coordinates have arrived. With just one of
// them, the bridge must keep asking the vehicle for HOME_POSITION; stopping
// early leaves the mission estimate without a home for the whole flight.
void TelemetryBridgeTest::testAskForHomeNeedsBothCoordinates()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    TelemetryBridge bridge;
    bridge._vehicle = _vehicle;   // no signal wiring: drive the decision directly

    bridge._homeLatitude = 37.4;
    bridge._homeLongitude = 0.0;
    bridge._homeAskTimer.start();
    bridge._askForHome();
    QVERIFY2(bridge._homeAskTimer.isActive(), "latitude alone is not a known home");

    bridge._homeLatitude = 0.0;
    bridge._homeLongitude = -122.1;
    bridge._homeAskTimer.start();
    bridge._askForHome();
    QVERIFY2(bridge._homeAskTimer.isActive(), "longitude alone is not a known home");

    bridge._homeLatitude = 37.4;
    bridge._homeLongitude = -122.1;
    bridge._homeAskTimer.start();
    bridge._askForHome();
    QVERIFY(!bridge._homeAskTimer.isActive());
}

// A HOME_POSITION message carrying only one coordinate must not stop the
// request timer: otherwise the _askForHome fix never gets a second chance.
void TelemetryBridgeTest::testPartialHomeMessageKeepsAsking()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    TelemetryBridge bridge;
    bridge.setVehicle(_vehicle);
    QVERIFY(bridge._homeAskTimer.isActive());

    emit _vehicle->homePositionChanged(QGeoCoordinate(37.4, 0.0, 0.0));
    QVERIFY2(bridge._homeAskTimer.isActive(), "a latitude-only HOME_POSITION is not a home fix");

    emit _vehicle->homePositionChanged(QGeoCoordinate(37.4, -122.1, 0.0));
    QVERIFY(!bridge._homeAskTimer.isActive());
}

// Switching vehicles clears the previous home. A half-known home is not a
// position to announce as changed while it is being cleared.
void TelemetryBridgeTest::testPartialHomeIsNotLostOnVehicleSwitch()
{
    _connectMockLink(MAV_AUTOPILOT_ARDUPILOTMEGA);
    TelemetryBridge bridge;
    bridge._vehicle = _vehicle;
    bridge._homeLatitude = 37.4;
    bridge._homeLongitude = 0.0;
    QSignalSpy homeChanged(&bridge, &TelemetryBridge::homePositionChanged);

    bridge.setVehicle(nullptr);
    QCOMPARE(homeChanged.count(), 0);
}

UT_REGISTER_TEST(TelemetryBridgeTest)

#include "TelemetryBridgeTest.moc"
