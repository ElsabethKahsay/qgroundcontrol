#pragma once

/// @file TelemetryBridge.h
/// @brief Central telemetry adapter that wraps a QGroundControl Vehicle and exposes
///        all vehicle telemetry as Q_PROPERTY values for QML consumption.
///
/// TelemetryBridge sits between the QGC Vehicle subsystem and our custom QML UI.
/// It subscribes to Vehicle Fact signals, raw MAVLink messages, and periodic timers,
/// then caches the latest values in member variables and exposes them as Qt properties.
/// Preflight checks and other QML components can read these properties directly
/// without coupling to the QGC Vehicle internals.
///
/// The bridge supports:
///   - Connection status and quality estimation
///   - Primary and secondary battery telemetry
///   - GPS (primary + secondary), home position, barometer
///   - RC link health, failsafe, channel values
///   - IMU, compass, accelerometer, gyroscope, magnetometer
///   - EKF/estimator status and variances
///   - ESC telemetry (temperatures, voltages, currents, RPM, error counts)
///   - Servo/motor outputs, gimbal state, optical flow
///   - Vibration, wind, airspeed, attitude
///   - Mission info (count, waypoint distances)
///   - Pre-arm check status and messages
///   - Vehicle and autopilot identification strings
///   - Parameter cache with watchlist-based subscriptions

#include <QObject>
#include <QDateTime>
#include <QVariantList>
#include <QString>
#include <QTimer>
#include <QVariant>

#include "QGCMAVLink.h"

class Vehicle;

/// @class TelemetryBridge
/// @brief Bridges MAVLink telemetry from a Vehicle object into Q_PROPERTY values
///        that can be consumed by QML and custom preflight-check components.
///
/// Usage: create an instance, call setVehicle() with the active Vehicle pointer,
///        then bind to any Q_PROPERTY from QML. The bridge auto-disconnects from
///        a previous vehicle when a new one is set.
class TelemetryBridge : public QObject {
    Q_OBJECT

    // ==================== Connection status ====================

    /// True when a Vehicle is set (i.e. a link to the autopilot exists).
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)

    /// Link quality 0-100, derived from heartbeat timing jitter and comm drop rate.
    Q_PROPERTY(int connectionQuality READ connectionQuality NOTIFY connectionQualityChanged)

    // ==================== Battery / power ====================

    /// Primary battery voltage in volts (from BATTERY_STATUS or FactGroup).
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)

    /// Primary battery remaining percentage (0-100), -1 if unknown.
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)

    /// Primary battery current draw in amps.
    Q_PROPERTY(double batteryCurrent READ batteryCurrent NOTIFY batteryCurrentChanged)

    /// Primary battery temperature in degrees Celsius, NaN if unavailable.
    Q_PROPERTY(double batteryTemperature READ batteryTemperature NOTIFY batteryTemperatureChanged)

    /// Primary battery charge state string ("OK", "low", "critical", "emergency").
    Q_PROPERTY(QString batteryChargeState READ batteryChargeState NOTIFY batteryChargeStateChanged)

    /// Primary battery current draw in amps (alias for batteryCurrent).
    Q_PROPERTY(double batteryCurrentAmps READ batteryCurrentAmps NOTIFY batteryCurrentAmpsChanged)

    /// Raw battery_remaining from the latest BATTERY_STATUS (0-100). -1 when
    /// the flight controller does not report SOC (battery not configured).
    Q_PROPERTY(int batterySocPct READ batterySocPct NOTIFY batterySocPctChanged)

    /// 10-second exponential moving average of battery current (amps),
    /// accumulated over every BATTERY_STATUS message (~1 Hz, alpha = 0.095).
    /// Primed from the first valid sample; never reset between messages.
    Q_PROPERTY(double batteryCurrentSmoothAmps READ batteryCurrentSmoothAmps NOTIFY batteryCurrentSmoothAmpsChanged)

    /// True when batterySocPct is >= 0 AND a BATTERY_STATUS was received within
    /// the last 5 seconds. False while stale or when the FC reports no SOC.
    Q_PROPERTY(bool batteryDataValid READ batteryDataValid NOTIFY batteryDataValidChanged)

    /// Primary battery estimated time remaining in seconds (-1 or NaN if unknown).
    Q_PROPERTY(double batteryTimeRemainingSeconds READ batteryTimeRemainingSeconds NOTIFY batteryTimeRemainingSecondsChanged)

    /// Per-cell voltages for the primary battery (list of doubles in volts).
    Q_PROPERTY(QVariantList batteryCellVoltages READ batteryCellVoltages NOTIFY batteryCellVoltagesChanged)

    /// System-level battery voltage from SYS_STATUS (power-module voltage, volts).
    Q_PROPERTY(double sysVoltageBattery READ sysVoltageBattery NOTIFY sysVoltageBatteryChanged)

    /// Secondary battery voltage in volts (0 if no second battery).
    Q_PROPERTY(double battery2Voltage READ battery2Voltage NOTIFY battery2VoltageChanged)

    /// Secondary battery remaining percentage (-1 if absent).
    Q_PROPERTY(int battery2Percent READ battery2Percent NOTIFY battery2PercentChanged)

    /// Secondary battery current in amps.
    Q_PROPERTY(double battery2Current READ battery2Current NOTIFY battery2CurrentChanged)

    /// Whether a second battery has been detected on the vehicle.
    Q_PROPERTY(bool battery2Present READ battery2Present NOTIFY battery2PresentChanged)

    /// Radio telemetry RSSI (0-255 from RADIO_STATUS).
    Q_PROPERTY(int radioRssi READ radioRssi NOTIFY radioStatusChanged)

    /// Number of transmitted packets waiting in the radio TX buffer.
    Q_PROPERTY(int radioTxBuf READ radioTxBuf NOTIFY radioStatusChanged)

    /// Count of RX errors reported by the radio modem.
    Q_PROPERTY(int radioRxErrors READ radioRxErrors NOTIFY radioStatusChanged)

    /// Terrain height above sea level at the vehicle's position (meters).
    Q_PROPERTY(double terrainHeight READ terrainHeight NOTIFY terrainReportChanged)

    /// True when a companion computer heartbeat has been seen (compid != 1).
    Q_PROPERTY(bool companionDetected READ companionDetected NOTIFY companionDetectedChanged)

    /// MAVLink protocol version reported in heartbeats.
    Q_PROPERTY(uint8_t mavlinkVersion READ mavlinkVersion NOTIFY mavlinkVersionChanged)

    // ==================== GPS primary ====================

    /// GPS fix type (0=none, 2=2D, 3=3D, 4=DGPS, 5=RTKFloat, 6=RTKFixed).
    Q_PROPERTY(int gpsFixType READ gpsFixType NOTIFY gpsFixTypeChanged)

    /// Number of satellites used by the primary GPS.
    Q_PROPERTY(int gpsSatellites READ gpsSatellites NOTIFY gpsSatellitesChanged)

    /// Primary GPS latitude in degrees.
    Q_PROPERTY(double gpsLatitude READ gpsLatitude NOTIFY gpsPositionChanged)

    /// Primary GPS longitude in degrees.
    Q_PROPERTY(double gpsLongitude READ gpsLongitude NOTIFY gpsPositionChanged)

    /// Horizontal dilution of precision from the primary GPS.
    Q_PROPERTY(double gpsHdop READ gpsHdop NOTIFY gpsHdopChanged)

    /// 3D speed accuracy estimate from GPS in m/s.
    Q_PROPERTY(double gpsSpeedAccuracy READ gpsSpeedAccuracy NOTIFY gpsSpeedAccuracyChanged)

    /// GPS altitude above mean sea level in meters.
    Q_PROPERTY(double gpsAltitude READ gpsAltitude NOTIFY gpsAltitudeChanged)

    // ==================== GPS secondary ====================

    /// Secondary GPS fix type (same encoding as gpsFixType).
    Q_PROPERTY(int gps2FixType READ gps2FixType NOTIFY gps2Changed)

    /// Number of satellites used by the secondary GPS.
    Q_PROPERTY(int gps2Satellites READ gps2Satellites NOTIFY gps2Changed)

    /// Secondary GPS latitude in degrees.
    Q_PROPERTY(double gps2Latitude READ gps2Latitude NOTIFY gps2Changed)

    /// Secondary GPS longitude in degrees.
    Q_PROPERTY(double gps2Longitude READ gps2Longitude NOTIFY gps2Changed)

    /// Horizontal position accuracy from the secondary GPS (cm, UINT16_MAX = invalid).
    Q_PROPERTY(uint16_t gps2Eph READ gps2Eph NOTIFY gps2Changed)

    /// Yaw reported by dual-antenna secondary GPS (centidegrees).
    Q_PROPERTY(uint16_t gps2Yaw READ gps2Yaw NOTIFY gps2Changed)

    // ==================== Home position ====================

    /// Latitude of the vehicle's home/launch position.
    Q_PROPERTY(double homeLatitude READ homeLatitude NOTIFY homePositionChanged)

    /// Longitude of the vehicle's home/launch position.
    Q_PROPERTY(double homeLongitude READ homeLongitude NOTIFY homePositionChanged)

    /// Altitude of the home position in meters.
    Q_PROPERTY(double homeAltitude READ homeAltitude NOTIFY homePositionChanged)

    // ==================== IMU / compass / baro ====================

    /// True when the IMU sensor health bit is set in SYS_STATUS.
    Q_PROPERTY(bool imuHealthy READ imuHealthy NOTIFY imuHealthyChanged)

    /// True when the compass sensor health bit is set in SYS_STATUS.
    Q_PROPERTY(bool compassHealthy READ compassHealthy NOTIFY compassHealthyChanged)

    /// IMU board temperature in degrees Celsius.
    Q_PROPERTY(double imuTemperature READ imuTemperature NOTIFY imuTemperatureChanged)

    /// Barometric pressure in hPa (absolute).
    Q_PROPERTY(double baroPressure READ baroPressure NOTIFY baroChanged)

    /// Barometer temperature in degrees Celsius.
    Q_PROPERTY(double baroTemperature READ baroTemperature NOTIFY baroChanged)

    // ==================== Airspeed / position ====================

    /// Indicated airspeed in m/s.
    Q_PROPERTY(double airspeed READ airspeed NOTIFY airspeedChanged)

    /// Ground speed in m/s.
    Q_PROPERTY(double groundSpeed READ groundSpeed NOTIFY groundSpeedChanged)

    /// Vertical (climb/descent) speed in m/s, positive = ascending.
    Q_PROPERTY(double verticalSpeed READ verticalSpeed NOTIFY verticalSpeedChanged)

    /// Vehicle heading / yaw in degrees (0-360).
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)

    /// Altitude relative to home in meters.
    Q_PROPERTY(double altitudeRelative READ altitudeRelative NOTIFY altitudeChanged)

    // ==================== RC link ====================

    /// RC receiver signal strength (0-255, from RC_CHANNELS).
    Q_PROPERTY(int rcRssi READ rcRssi NOTIFY rcRssiChanged)

    /// True when the RC receiver is connected and sending valid channel data.
    Q_PROPERTY(bool rcConnected READ rcConnected NOTIFY rcConnectedChanged)

    /// True when the RC link is in failsafe (no signal or chancount==0).
    Q_PROPERTY(bool rcFailsafe READ rcFailsafe NOTIFY rcFailsafeChanged)

    /// Raw RC channel PWM values (list of ints, channels 1-18).
    Q_PROPERTY(QVariantList rcChannelValues READ rcChannelValues NOTIFY rcChannelValuesChanged)

    /// Timestamp of the last RC_CHANNELS message in microseconds.
    Q_PROPERTY(qint64 rcLastUpdateUsec READ rcLastUpdateUsec NOTIFY rcLastUpdateChanged)

    // ==================== Arming / flight mode ====================

    /// Whether the vehicle is currently armed.
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)

    /// Current flight mode string (e.g. "Stabilize", "Loiter", "Auto").
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)

    /// True once at least one heartbeat has been received from the autopilot.
    Q_PROPERTY(bool heartbeatReceived READ heartbeatReceived NOTIFY heartbeatReceivedChanged)

    // ==================== Vibration ====================

    /// Vibration level on the X axis (m/s^2, from VIBRATION message).
    Q_PROPERTY(double vibrationX READ vibrationX NOTIFY vibrationChanged)

    /// Vibration level on the Y axis (m/s^2).
    Q_PROPERTY(double vibrationY READ vibrationY NOTIFY vibrationChanged)

    /// Vibration level on the Z axis (m/s^2).
    Q_PROPERTY(double vibrationZ READ vibrationZ NOTIFY vibrationChanged)

    /// Number of clipping events detected on any accelerometer axis.
    Q_PROPERTY(uint vibrationClipping READ vibrationClipping NOTIFY vibrationChanged)

    // ==================== EKF estimator status ====================

    /// Raw EKF status flags bitmask (from ESTIMATOR_STATUS or EKF_STATUS_REPORT).
    Q_PROPERTY(uint estimatorFlags READ estimatorFlags NOTIFY estimatorStatusChanged)

    /// EKF velocity consistency check ratio (0 = perfect, higher = worse).
    Q_PROPERTY(double estimatorVelRatio READ estimatorVelRatio NOTIFY estimatorStatusChanged)

    /// EKF horizontal position consistency ratio.
    Q_PROPERTY(double estimatorPosHorizRatio READ estimatorPosHorizRatio NOTIFY estimatorStatusChanged)

    /// EKF vertical position consistency ratio.
    Q_PROPERTY(double estimatorPosVertRatio READ estimatorPosVertRatio NOTIFY estimatorStatusChanged)

    /// EKF magnetometer consistency ratio.
    Q_PROPERTY(double estimatorMagRatio READ estimatorMagRatio NOTIFY estimatorStatusChanged)

    // ==================== AHRS / sensor health ====================

    /// True when the AHRS health flag is set (attitude estimation OK).
    Q_PROPERTY(bool ahrsHealth READ ahrsHealth NOTIFY ahrsHealthChanged)

    /// Bitmask of all onboard sensor health flags from SYS_STATUS.
    Q_PROPERTY(uint sensorHealth READ sensorHealth NOTIFY sensorHealthChanged)

    /// Communication drop rate as a percentage (from SYS_STATUS).
    Q_PROPERTY(double commDropRate READ commDropRate NOTIFY commDropRateChanged)

    // ==================== Wind ====================

    /// Estimated wind speed in m/s (from WindFactGroup / WIND_COV).
    Q_PROPERTY(double windSpeed READ windSpeed NOTIFY windChanged)

    /// Estimated wind direction in degrees (0 = from north).
    Q_PROPERTY(double windDirection READ windDirection NOTIFY windChanged)

    // ==================== Attitude / gyro ====================

    /// Gyro X-axis rate in rad/s.
    Q_PROPERTY(double gyroX READ gyroX NOTIFY gyroChanged)

    /// Gyro Y-axis rate in rad/s.
    Q_PROPERTY(double gyroY READ gyroY NOTIFY gyroChanged)

    /// Gyro Z-axis rate in rad/s.
    Q_PROPERTY(double gyroZ READ gyroZ NOTIFY gyroChanged)

    /// Roll angle in radians (from ATTITUDE message).
    Q_PROPERTY(double roll READ roll NOTIFY attitudeChanged)

    /// Pitch angle in radians.
    Q_PROPERTY(double pitch READ pitch NOTIFY attitudeChanged)

    /// Yaw angle in radians.
    Q_PROPERTY(double yaw READ yaw NOTIFY attitudeChanged)

    /// Mean-sea-level altitude in meters (from GLOBAL_POSITION_INT).
    Q_PROPERTY(double globalAltitude READ globalAltitude NOTIFY globalAltitudeChanged)

    // ==================== Optical flow / gimbal ====================

    /// Optical flow sensor quality metric (0-255, -1 if no sensor).
    Q_PROPERTY(int opticalFlowQuality READ opticalFlowQuality NOTIFY opticalFlowQualityChanged)

    /// True when a gimbal has been detected on the MAVLink bus.
    Q_PROPERTY(bool gimbalDetected READ gimbalDetected NOTIFY gimbalDetectedChanged)

    /// Gimbal control mode (1 = MAVLink, 2 = RC, 3 = auto).
    Q_PROPERTY(int gimbalMode READ gimbalMode NOTIFY gimbalModeChanged)

    // ==================== EKF variances ====================

    /// EKF velocity variance estimate.
    Q_PROPERTY(double ekfVelVariance READ ekfVelVariance NOTIFY ekfVarianceChanged)

    /// EKF horizontal position variance.
    Q_PROPERTY(double ekfPosHorizVariance READ ekfPosHorizVariance NOTIFY ekfVarianceChanged)

    /// EKF vertical position variance.
    Q_PROPERTY(double ekfPosVertVariance READ ekfPosVertVariance NOTIFY ekfVarianceChanged)

    /// EKF compass/magnetometer variance.
    Q_PROPERTY(double ekfCompassVariance READ ekfCompassVariance NOTIFY ekfVarianceChanged)

    /// EKF terrain altitude variance.
    Q_PROPERTY(double ekfTerrainVariance READ ekfTerrainVariance NOTIFY ekfVarianceChanged)

    // ==================== Pre-arm status ====================

    /// True when all pre-arm checks have passed (vehicle is ready to arm).
    Q_PROPERTY(bool preArmOk READ preArmOk NOTIFY preArmOkChanged)

    /// Human-readable pre-arm failure message (empty when checks pass).
    Q_PROPERTY(QString preArmMessage READ preArmMessage NOTIFY preArmMessageChanged)

    // ==================== Motors ====================

    /// Number of active motor outputs (highest non-zero servo channel index).
    Q_PROPERTY(int motorCount READ motorCount NOTIFY motorCountChanged)

    /// PWM output values for all 16 servo/motor channels (from SERVO_OUTPUT_RAW).
    Q_PROPERTY(QVariantList motorOutputs READ motorOutputs NOTIFY servoOutputsChanged)

    // ==================== Mission ====================

    /// Total number of mission items (waypoints) loaded on the vehicle.
    Q_PROPERTY(int missionCount READ missionCount NOTIFY missionCountChanged)

    /// Straight-line distance from home to the first waypoint in meters.
    Q_PROPERTY(double missionFirstWpDistance READ missionFirstWpDistance NOTIFY missionFirstWpDistanceChanged)

    /// Total Haversine distance across all consecutive waypoints in meters.
    Q_PROPERTY(double missionTotalDistance READ missionTotalDistance NOTIFY missionTotalDistanceChanged)

    // ==================== Accelerometers ====================

    /// Primary accelerometer X-axis reading (m/s^2).
    Q_PROPERTY(double accelerometerX READ accelerometerX NOTIFY accelerometerChanged)

    /// Primary accelerometer Y-axis reading (m/s^2).
    Q_PROPERTY(double accelerometerY READ accelerometerY NOTIFY accelerometerChanged)

    /// Primary accelerometer Z-axis reading (m/s^2).
    Q_PROPERTY(double accelerometerZ READ accelerometerZ NOTIFY accelerometerChanged)

    /// Secondary accelerometer X-axis reading (m/s^2).
    Q_PROPERTY(double accelerometer2X READ accelerometer2X NOTIFY accelerometer2Changed)

    /// Secondary accelerometer Y-axis reading (m/s^2).
    Q_PROPERTY(double accelerometer2Y READ accelerometer2Y NOTIFY accelerometer2Changed)

    /// Secondary accelerometer Z-axis reading (m/s^2).
    Q_PROPERTY(double accelerometer2Z READ accelerometer2Z NOTIFY accelerometer2Changed)

    // ==================== ESC telemetry ====================

    /// ESC temperatures in degrees Celsius (per-motor list).
    Q_PROPERTY(QVariantList escTemperatures READ escTemperatures NOTIFY escTelemetryChanged)

    /// ESC input voltages in volts (per-motor list).
    Q_PROPERTY(QVariantList escVoltages READ escVoltages NOTIFY escTelemetryChanged)

    /// ESC current draw in amps (per-motor list).
    Q_PROPERTY(QVariantList escCurrents READ escCurrents NOTIFY escTelemetryChanged)

    /// ESC motor RPM values (per-motor list).
    Q_PROPERTY(QVariantList escRpm READ escRpm NOTIFY escTelemetryChanged)

    /// Number of ESCs reporting info (from ESC_INFO message).
    Q_PROPERTY(int escInfoCount READ escInfoCount NOTIFY escInfoChanged)

    /// Per-ESC failure flag bitmasks (from ESC_INFO).
    Q_PROPERTY(QVariantList escInfoFailureFlags READ escInfoFailureFlags NOTIFY escInfoChanged)

    /// Per-ESC error counts (from ESC_INFO).
    Q_PROPERTY(QVariantList escInfoErrorCount READ escInfoErrorCount NOTIFY escInfoChanged)

    /// ESC connection type (0=unknown, 1=UART, 2=CAN, 3=DSHOT).
    Q_PROPERTY(int escInfoConnectionType READ escInfoConnectionType NOTIFY escInfoChanged)

    // ==================== Magnetic field ====================

    /// Magnetometer X-axis field strength in gauss.
    Q_PROPERTY(double magFieldX READ magFieldX NOTIFY magFieldChanged)

    /// Magnetometer Y-axis field strength in gauss.
    Q_PROPERTY(double magFieldY READ magFieldY NOTIFY magFieldChanged)

    /// Magnetometer Z-axis field strength in gauss.
    Q_PROPERTY(double magFieldZ READ magFieldZ NOTIFY magFieldChanged)

    // ==================== Vehicle identification ====================

    /// Human-readable vehicle type string (e.g. "Quadcopter", "VTOL").
    Q_PROPERTY(QString vehicleType READ vehicleType NOTIFY vehicleTypeChanged)

    /// Connection state description (e.g. "Connected", "Disconnected").
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

    /// Connection URL or address string.
    Q_PROPERTY(QString connectionUrl READ connectionUrl NOTIFY connectionUrlChanged)

    /// Autopilot firmware type string (e.g. "PX4", "ArduPilot").
    Q_PROPERTY(QString autopilotType READ autopilotType NOTIFY autopilotTypeChanged)

    // ==================== GPS status strings ====================

    /// Human-readable GPS fix type label (e.g. "3D Fix", "No Fix").
    Q_PROPERTY(QString gpsFixTypeString READ gpsFixTypeString NOTIFY gpsFixTypeChanged)

    /// GPS data quality score (0=poor, 1=fair, 2=good, 3=excellent).
    Q_PROPERTY(int gpsDataQuality READ gpsDataQuality NOTIFY gpsDataQualityChanged)

    /// Combined GPS status description string for display.
    Q_PROPERTY(QString gpsStatusString READ gpsStatusString NOTIFY gpsDataQualityChanged)

    // ==================== Battery status strings ====================

    /// Battery data quality score (0=poor .. 3=excellent).
    Q_PROPERTY(int batteryDataQuality READ batteryDataQuality NOTIFY batteryDataQualityChanged)

    /// Human-readable battery status summary string.
    Q_PROPERTY(QString batteryStatusString READ batteryStatusString NOTIFY batteryDataQualityChanged)

    // ==================== Servo / motor output strings ====================

    /// Formatted string of all servo/motor PWM outputs for display.
    Q_PROPERTY(QString servoOutputsString READ servoOutputsString NOTIFY servoOutputsChanged)

    // ==================== Gimbal ====================

    /// Gimbal pitch angle in degrees (negative = down).
    Q_PROPERTY(double gimbalPitch READ gimbalPitch NOTIFY gimbalAttitudeChanged)

    /// Gimbal roll angle in degrees.
    Q_PROPERTY(double gimbalRoll READ gimbalRoll NOTIFY gimbalAttitudeChanged)

    /// Gimbal yaw angle in degrees.
    Q_PROPERTY(double gimbalYaw READ gimbalYaw NOTIFY gimbalAttitudeChanged)

    /// True when the gimbal is currently in its calibration sequence.
    Q_PROPERTY(bool gimbalCalibrating READ gimbalCalibrating NOTIFY gimbalCalibratingChanged)

    // ==================== EKF extras ====================

    /// EKF airspeed variance estimate.
    Q_PROPERTY(double ekfAirspeedVariance READ ekfAirspeedVariance NOTIFY ekfVarianceChanged)

    /// Human-readable EKF status summary string.
    Q_PROPERTY(QString ekfStatus READ ekfStatus NOTIFY ekfStatusChanged)

    // ==================== Flight time / logging ====================

    /// Total armed/flight time in seconds.
    Q_PROPERTY(double flightTime READ flightTime NOTIFY flightTimeChanged)

    /// Timestamp string of the most recent log entry.
    Q_PROPERTY(QString lastLogTimestamp READ lastLogTimestamp NOTIFY lastLogTimestampChanged)

    // ==================== Pre-arm severity ====================

    /// Severity level of the last pre-arm message ("Info", "Warning", "Error").
    Q_PROPERTY(QString preArmSeverity READ preArmSeverity NOTIFY preArmSeverityChanged)

    // ==================== Sensor quality indicators ====================

    /// IMU data quality score (0=poor .. 3=excellent).
    Q_PROPERTY(int imuDataQuality READ imuDataQuality NOTIFY imuDataQualityChanged)

    /// Compass data quality score (0=poor .. 3=excellent).
    Q_PROPERTY(int compassDataQuality READ compassDataQuality NOTIFY compassDataQualityChanged)

    /// RC link data quality score (0=poor .. 3=excellent).
    Q_PROPERTY(int rcDataQuality READ rcDataQuality NOTIFY rcDataQualityChanged)

    // ==================== Hardware setup ====================

    /// True when the vehicle requires initial hardware setup (calibration, etc.).
    Q_PROPERTY(bool hardwareSetupRequired READ hardwareSetupRequired NOTIFY hardwareSetupRequiredChanged)

public:
    /// Construct a telemetry bridge. Does not connect to any vehicle until
    /// setVehicle() is called.
    explicit TelemetryBridge(QObject *parent = nullptr);

    /// Set the Vehicle to bridge telemetry from. Disconnects from any previously
    /// set vehicle first. Starts a 200ms polling timer when a vehicle is provided.
    void setVehicle(Vehicle* vehicle);

    /// Currently bridged vehicle, or nullptr.
    Vehicle* vehicle() const { return _vehicle; }

    /// Whether the vehicle is currently connected (non-null vehicle pointer).
    virtual bool isConnected() const;

    /// Link quality 0-100 derived from heartbeat jitter and packet drop rate.
    virtual int connectionQuality() const { return _connectionQuality; }

    virtual double batteryVoltage() const { return _batteryVoltage; }
    int batteryPercent() const { return _batteryPercent; }
    double batteryCurrent() const { return _batteryCurrent; }
    double batteryCurrentAmps() const { return _batteryCurrentAmps; }
    int batterySocPct() const { return _batterySocPct; }
    double batteryCurrentSmoothAmps() const { return _batteryCurrentSmoothAmps; }
    bool batteryDataValid() const { return _batteryDataValid; }
    double batteryTemperature() const { return _batteryTemperature; }
    QString batteryChargeState() const { return _batteryChargeState; }
    double batteryTimeRemainingSeconds() const { return _batteryTimeRemainingSeconds; }
    QVariantList batteryCellVoltages() const { return _batteryCellVoltages; }
    double sysVoltageBattery() const { return _sysVoltageBattery; }
    double battery2Voltage() const { return _battery2Voltage; }
    int battery2Percent() const { return _battery2Percent; }
    double battery2Current() const { return _battery2Current; }
    bool battery2Present() const { return _battery2Present; }
    int radioRssi() const { return _radioRssi; }
    int radioTxBuf() const { return _radioTxBuf; }
    int radioRxErrors() const { return _radioRxErrors; }
    double terrainHeight() const { return _terrainHeight; }
    bool companionDetected() const { return _companionDetected; }
    uint8_t mavlinkVersion() const { return _mavlinkVersion; }

    int gpsFixType() const { return _gpsFixType; }
    int gpsSatellites() const { return _gpsSatellites; }
    double gpsLatitude() const { return _gpsLatitude; }
    double gpsLongitude() const { return _gpsLongitude; }
    double gpsHdop() const { return _gpsHdop; }
    double gpsSpeedAccuracy() const { return _gpsSpeedAccuracy; }
    double gpsAltitude() const { return _gpsAltitude; }
    int gps2FixType() const { return _gps2FixType; }
    int gps2Satellites() const { return _gps2Satellites; }
    double gps2Latitude() const { return _gps2Latitude; }
    double gps2Longitude() const { return _gps2Longitude; }
    uint16_t gps2Eph() const { return _gps2Eph; }
    uint16_t gps2Yaw() const { return _gps2Yaw; }

    double homeLatitude() const { return _homeLatitude; }
    double homeLongitude() const { return _homeLongitude; }
    double homeAltitude() const { return _homeAltitude; }

    bool imuHealthy() const { return _imuHealthy; }
    bool compassHealthy() const { return _compassHealthy; }
    double imuTemperature() const { return _imuTemperature; }
    double baroPressure() const { return _baroPressure; }
    double baroTemperature() const { return _baroTemperature; }

    double airspeed() const { return _airspeed; }
    double groundSpeed() const { return _groundSpeed; }
    double verticalSpeed() const { return _verticalSpeed; }
    double heading() const { return _heading; }
    double altitudeRelative() const { return _altitudeRelative; }

    int rcRssi() const { return _rcRssi; }
    bool rcConnected() const { return _rcConnected; }
    bool rcFailsafe() const { return _rcFailsafe; }
    QVariantList rcChannelValues() const { return _rcChannelValues; }
    qint64 rcLastUpdateUsec() const { return _rcLastUpdateUsec; }

    bool armed() const;
    QString flightMode() const;
    bool heartbeatReceived() const { return _heartbeatReceived; }

    double vibrationX() const { return _vibrationX; }
    double vibrationY() const { return _vibrationY; }
    double vibrationZ() const { return _vibrationZ; }
    uint vibrationClipping() const { return _vibrationClipping; }

    uint estimatorFlags() const { return _estimatorFlags; }
    double estimatorVelRatio() const { return _estimatorVelRatio; }
    double estimatorPosHorizRatio() const { return _estimatorPosHorizRatio; }
    double estimatorPosVertRatio() const { return _estimatorPosVertRatio; }
    double estimatorMagRatio() const { return _estimatorMagRatio; }

    bool ahrsHealth() const { return _ahrsHealth; }
    uint sensorHealth() const { return _sensorHealth; }
    double commDropRate() const { return _commDropRate; }

    double windSpeed() const { return _windSpeed; }
    double windDirection() const { return _windDirection; }

    double gyroX() const { return _gyroX; }
    double gyroY() const { return _gyroY; }
    double gyroZ() const { return _gyroZ; }
    double roll() const { return _roll; }
    double pitch() const { return _pitch; }
    double yaw() const { return _yaw; }
    double globalAltitude() const { return _globalAltitude; }

    int opticalFlowQuality() const { return _opticalFlowQuality; }
    bool gimbalDetected() const { return _gimbalDetected; }
    int gimbalMode() const { return _gimbalMode; }

    double ekfVelVariance() const { return _ekfVelVariance; }
    double ekfPosHorizVariance() const { return _ekfPosHorizVariance; }
    double ekfPosVertVariance() const { return _ekfPosVertVariance; }
    double ekfCompassVariance() const { return _ekfCompassVariance; }
    double ekfTerrainVariance() const { return _ekfTerrainVariance; }

    bool preArmOk() const { return _preArmOk; }
    QString preArmMessage() const { return _preArmMessage; }

    int motorCount() const { return _motorCount; }
    QVariantList motorOutputs() const { return _motorOutputs; }

    virtual int missionCount() const { return _missionCount; }
    virtual double missionFirstWpDistance() const { return _missionFirstWpDistance; }
    virtual double missionTotalDistance() const { return _missionTotalDistance; }

    double accelerometerX() const { return _accelerometerX; }
    double accelerometerY() const { return _accelerometerY; }
    double accelerometerZ() const { return _accelerometerZ; }
    double accelerometer2X() const { return _accelerometer2X; }
    double accelerometer2Y() const { return _accelerometer2Y; }
    double accelerometer2Z() const { return _accelerometer2Z; }

    QVariantList escTemperatures() const { return _escTemperatures; }
    QVariantList escVoltages() const { return _escVoltages; }
    QVariantList escCurrents() const { return _escCurrents; }
    QVariantList escRpm() const { return _escRpm; }
    int escInfoCount() const { return _escInfoCount; }
    QVariantList escInfoFailureFlags() const { return _escInfoFailureFlags; }
    QVariantList escInfoErrorCount() const { return _escInfoErrorCount; }
    int escInfoConnectionType() const { return _escInfoConnectionType; }

    double magFieldX() const { return _magFieldX; }
    double magFieldY() const { return _magFieldY; }
    double magFieldZ() const { return _magFieldZ; }

    // -- Vehicle identification --
    QString vehicleType() const { return _vehicleType; }
    QString connectionStatus() const { return _connectionStatus; }
    QString connectionUrl() const { return _connectionUrl; }
    QString autopilotType() const { return _autopilotType; }

    // -- GPS status strings --
    QString gpsFixTypeString() const { return _gpsFixTypeString; }
    int gpsDataQuality() const { return _gpsDataQuality; }
    QString gpsStatusString() const { return _gpsStatusString; }

    // -- Battery status --
    int batteryDataQuality() const { return _batteryDataQuality; }
    QString batteryStatusString() const { return _batteryStatusString; }

    // -- Servo / motor output strings --
    QString servoOutputsString() const { return _servoOutputsString; }

    // -- Gimbal --
    double gimbalPitch() const { return _gimbalPitch; }
    double gimbalRoll() const { return _gimbalRoll; }
    double gimbalYaw() const { return _gimbalYaw; }
    bool gimbalCalibrating() const { return _gimbalCalibrating; }

    // -- EKF extras --
    double ekfAirspeedVariance() const { return _ekfAirspeedVariance; }
    QString ekfStatus() const { return _ekfStatus; }

    // -- Flight time / logging --
    double flightTime() const { return _flightTime; }
    QString lastLogTimestamp() const { return _lastLogTimestamp; }

    // -- Pre-arm severity --
    QString preArmSeverity() const { return _preArmSeverity; }

    // -- Sensor quality indicators --
    int imuDataQuality() const { return _imuDataQuality; }
    int compassDataQuality() const { return _compassDataQuality; }
    int rcDataQuality() const { return _rcDataQuality; }

    // -- Hardware setup --
    bool hardwareSetupRequired() const { return _hardwareSetupRequired; }

    /// Send an arm command to the vehicle via MAV_CMD_COMPONENT_ARM_DISARM.
    /// If an ArmingGate is set, the command is only sent when the gate is open
    /// or an override is active; otherwise it is silently blocked.
    Q_INVOKABLE void arm();

    /// Force-arm the vehicle, bypassing all pre-arm checks.
    /// Sends MAV_CMD_COMPONENT_ARM_DISARM with param1=1 and param2=21196 (ArduPilot
    /// magic number). Does NOT consult the ArmingGate. Use with caution.
    Q_INVOKABLE void forceArm();

    /// Set the arming gate reference so arm() can check gate state before sending.
    void setArmingGate(QObject* gate);

    /// Set a parameter value in the local cache. Also updates the Qt dynamic
    /// property so QML bindings react immediately.
    Q_INVOKABLE void setParameterValue(const QString& name, float value);

    /// Check whether a parameter exists in the local watchlist cache.
    Q_INVOKABLE bool hasParameter(const QString& name) const;

    /// Read a parameter value from the local cache, returning defaultValue if absent.
    Q_INVOKABLE float parameterValue(const QString& name, float defaultValue = 0.0f) const;

    /// True once the parameter cache has been populated from the ParameterManager.
    bool parametersReady() const { return _parametersReady; }

signals:
    /// Emitted when the parameter cache transitions to ready (all watchlist params loaded).
    void parametersReadyChanged(bool ready);

    /// Emitted whenever a watchlist parameter's value changes on the vehicle.
    void parameterUpdated(const QString &name, float value);
    void isConnectedChanged();
    void connectionQualityChanged();
    void batteryVoltageChanged();
    void batteryPercentChanged();
    void batteryCurrentChanged();
    void batteryCurrentAmpsChanged();
    void batterySocPctChanged();
    void batteryCurrentSmoothAmpsChanged();
    void batteryDataValidChanged();
    void batteryTemperatureChanged();
    void batteryChargeStateChanged();
    void batteryTimeRemainingSecondsChanged();
    void batteryCellVoltagesChanged();
    void sysVoltageBatteryChanged();
    void battery2VoltageChanged();
    void battery2PercentChanged();
    void battery2CurrentChanged();
    void battery2PresentChanged();
    void radioStatusChanged();
    void terrainReportChanged();
    void companionDetectedChanged();
    void mavlinkVersionChanged();
    void gpsFixTypeChanged();
    void gpsSatellitesChanged();
    void gpsPositionChanged();
    void gpsHdopChanged();
    void gpsSpeedAccuracyChanged();
    void gpsAltitudeChanged();
    void gps2Changed();
    void homePositionChanged();
    void imuHealthyChanged();
    void compassHealthyChanged();
    void imuTemperatureChanged();
    void baroChanged();
    void airspeedChanged();
    void groundSpeedChanged();
    void verticalSpeedChanged();
    void headingChanged();
    void altitudeChanged();
    void rcRssiChanged();
    void rcConnectedChanged();
    void rcFailsafeChanged();
    void rcChannelValuesChanged();
    void rcLastUpdateChanged();
    void armedChanged();
    void flightModeChanged();
    void heartbeatReceivedChanged();
    void vibrationChanged();
    void estimatorStatusChanged();
    void ahrsHealthChanged();
    void sensorHealthChanged();
    void commDropRateChanged();
    void windChanged();
    void gyroChanged();
    void attitudeChanged();
    void globalAltitudeChanged();
    void opticalFlowQualityChanged();
    void gimbalDetectedChanged();
    void gimbalModeChanged();
    void ekfVarianceChanged();
    void preArmOkChanged();
    void preArmMessageChanged();
    void motorCountChanged();
    void servoOutputsChanged();
    void missionCountChanged();
    void missionFirstWpDistanceChanged();
    void missionTotalDistanceChanged();
    void accelerometerChanged();
    void accelerometer2Changed();
    void escTelemetryChanged();
    void escInfoChanged();
    void magFieldChanged();
    void vehicleTypeChanged();
    void connectionStatusChanged();
    void connectionUrlChanged();
    void autopilotTypeChanged();
    void gpsDataQualityChanged();
    void batteryDataQualityChanged();
    void gimbalAttitudeChanged();
    void gimbalCalibratingChanged();
    void ekfStatusChanged();
    void flightTimeChanged();
    void lastLogTimestampChanged();
    void preArmSeverityChanged();
    void imuDataQualityChanged();
    void compassDataQualityChanged();
    void rcDataQualityChanged();
    void hardwareSetupRequiredChanged();

private slots:
    /// Called when the Vehicle's connected state changes; clears heartbeat flag on disconnect.
    void _onVehicleConnectedChanged(bool connected);

    /// Called when ParameterManager finishes loading; triggers _loadParameters().
    void _onParameterReadyChanged(bool ready);

    /// Timer callback (200ms) that polls Vehicle for any values not pushed via signals.
    void _onTelemetryUpdate();

    /// Dispatches raw MAVLink messages to the appropriate property updates.
    void _handleMavlinkMessage(const mavlink_message_t& message);

private:
    /// Wires up all Vehicle signals (armed, flight mode, GPS, FactGroups, etc.).
    void _connectVehicleSignals();

    /// Iterates the battery list model and connects Fact signals for primary/secondary batteries.
    void _connectBatteryFacts(QObject* batteryModel);

    /// Loads watchlist parameters from ParameterManager into _parameterCache
    /// and sets up per-parameter change subscriptions.
    void _loadParameters();

    /// Periodic poll: ensures parameters are loaded once the ParameterManager is ready.
    void _updateFromVehicle();

    /// Recalculates mission waypoint count and distances after mission changes.
    void _updateMissionInfo();

    Vehicle* _vehicle = nullptr;          ///< Currently bridged vehicle (null when disconnected).
    int _connectionQuality = 0;           ///< Link quality 0-100.
    QTimer _updateTimer;                  ///< 200ms poll timer for values without push signals.
    QDateTime _lastHeartbeatTime;         ///< Timestamp of the most recent heartbeat (for quality calc).

    // -- Primary battery --
    double _batteryVoltage = 0.0;
    int _batteryPercent = 0;
    double _batteryCurrent = 0.0;
    double _batteryCurrentAmps = 0.0;
    int _batterySocPct = -1;                    ///< battery_remaining (0-100), -1 = FC not configured.
    double _batteryCurrentSmoothAmps = 0.0;     ///< 10s EMA accumulated per BATTERY_STATUS message.
    bool _batteryDataValid = false;             ///< SOC known AND BATTERY_STATUS < 5 s old.
    QDateTime _lastBatteryStatusTime;           ///< Timestamp of the most recent BATTERY_STATUS.
    double _batteryTemperature = qQNaN();       ///< NaN = no temperature sensor.
    QString _batteryChargeState = QStringLiteral("OK");
    double _batteryTimeRemainingSeconds = qQNaN();
    QVariantList _batteryCellVoltages;
    double _sysVoltageBattery = 0.0;            ///< Voltage from SYS_STATUS power module.

    // -- Secondary battery --
    double _battery2Voltage = 0.0;
    int _battery2Percent = -1;                   ///< -1 = secondary battery absent.
    double _battery2Current = 0.0;
    bool _battery2Present = false;               ///< Set true on first battery2 Fact update.

    // -- Radio / telemetry link --
    int _radioRssi = 0;
    int _radioTxBuf = 0;
    int _radioRxErrors = 0;

    // -- Misc connection --
    double _terrainHeight = qQNaN();
    bool _companionDetected = false;             ///< True once a non-autopilot compid is seen.
    uint8_t _mavlinkVersion = 0;

    int _gpsFixType = 0;
    int _gpsSatellites = 0;
    double _gpsLatitude = 0.0;
    double _gpsLongitude = 0.0;
    double _gpsHdop = -1.0;
    double _gpsSpeedAccuracy = -1.0;
    double _gpsAltitude = qQNaN();
    int _gps2FixType = 0;
    int _gps2Satellites = 0;
    double _gps2Latitude = 0.0;
    double _gps2Longitude = 0.0;
    uint16_t _gps2Eph = UINT16_MAX;
    uint16_t _gps2Yaw = UINT16_MAX;

    double _homeLatitude = 0.0;
    double _homeLongitude = 0.0;
    double _homeAltitude = 0.0;

    bool _imuHealthy = false;
    bool _compassHealthy = false;
    double _imuTemperature = qQNaN();
    double _baroPressure = qQNaN();
    double _baroTemperature = qQNaN();

    double _airspeed = 0.0;
    double _groundSpeed = 0.0;
    double _verticalSpeed = 0.0;
    double _heading = 0.0;
    double _altitudeRelative = 0.0;

    int _rcRssi = 0;
    bool _rcConnected = false;
    bool _rcFailsafe = false;
    QVariantList _rcChannelValues;
    qint64 _rcLastUpdateUsec = 0;

    double _vibrationX = 0.0;
    double _vibrationY = 0.0;
    double _vibrationZ = 0.0;
    uint _vibrationClipping = 0;

    uint _estimatorFlags = 0;
    double _estimatorVelRatio = 0.0;
    double _estimatorPosHorizRatio = 0.0;
    double _estimatorPosVertRatio = 0.0;
    double _estimatorMagRatio = 0.0;

    bool _ahrsHealth = false;
    uint _sensorHealth = 0;
    double _commDropRate = 0.0;

    double _windSpeed = 0.0;
    double _windDirection = 0.0;

    double _gyroX = qQNaN();
    double _gyroY = qQNaN();
    double _gyroZ = qQNaN();
    double _roll = 0.0;
    double _pitch = 0.0;
    double _yaw = 0.0;
    double _globalAltitude = qQNaN();

    int _opticalFlowQuality = -1;
    bool _gimbalDetected = false;
    int _gimbalMode = 1;

    double _ekfVelVariance = 0.0;
    double _ekfPosHorizVariance = 0.0;
    double _ekfPosVertVariance = 0.0;
    double _ekfCompassVariance = 0.0;
    double _ekfTerrainVariance = 0.0;

    bool _preArmOk = false;
    QString _preArmMessage;

    bool _heartbeatReceived = false;

    int _motorCount = 0;
    QVariantList _motorOutputs;
    int _fwMotorChannel = -1;   /// Cached fixed-wing motor output channel (1-based), -1 until detected

    /// Auto-detects the servo output channel driving the fixed-wing motor
    /// (SERVOn_FUNCTION = 70/73, falling back to the RC throttle channel).
    int _fwMotorOutputChannel();

    int _missionCount = 0;
    double _missionFirstWpDistance = -1.0;
    double _missionTotalDistance = -1.0;

    double _accelerometerX = qQNaN();
    double _accelerometerY = qQNaN();
    double _accelerometerZ = qQNaN();
    double _accelerometer2X = qQNaN();
    double _accelerometer2Y = qQNaN();
    double _accelerometer2Z = qQNaN();

    QVariantList _escTemperatures;
    QVariantList _escVoltages;
    QVariantList _escCurrents;
    QVariantList _escRpm;
    int _escInfoCount = 0;
    QVariantList _escInfoFailureFlags;
    QVariantList _escInfoErrorCount;
    int _escInfoConnectionType = 0;

    double _magFieldX = qQNaN();
    double _magFieldY = qQNaN();
    double _magFieldZ = qQNaN();

    // -- Vehicle identification --
    QString _vehicleType;
    QString _connectionStatus;
    QString _connectionUrl;
    QString _autopilotType;

    // -- GPS status --
    QString _gpsFixTypeString;
    int _gpsDataQuality = 2;
    QString _gpsStatusString;

    // -- Battery status --
    int _batteryDataQuality = 2;
    QString _batteryStatusString;

    // -- Servo outputs string --
    QString _servoOutputsString;

    // -- Gimbal --
    double _gimbalPitch = 0.0;
    double _gimbalRoll = 0.0;
    double _gimbalYaw = 0.0;
    bool _gimbalCalibrating = false;

    // -- EKF extras --
    double _ekfAirspeedVariance = 0.0;
    QString _ekfStatus;

    // -- Flight time / logging --
    double _flightTime = 0.0;
    QString _lastLogTimestamp;

    // -- Pre-arm severity --
    QString _preArmSeverity;

    // -- Sensor quality --
    int _imuDataQuality = 3;
    int _compassDataQuality = 3;
    int _rcDataQuality = 2;

    // -- Hardware setup --
    bool _hardwareSetupRequired = false;

    QMap<QString, float> _parameterCache;           ///< Watchlist parameter name -> cached value.
    QVector<QMetaObject::Connection> _paramConnections; ///< Active per-parameter Fact::valueChanged connections.
    bool _parametersReady = false;                 ///< True after _loadParameters() completes.
    QObject* _armingGate = nullptr;                ///< ArmingGate for arm-command gating (set by PreflightPlugin).
};
