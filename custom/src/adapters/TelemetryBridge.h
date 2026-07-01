#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QTimer>
#include <QVariant>

#include "QGCMAVLink.h"

/// @file TelemetryBridge.h
/// Bridges MAVLink telemetry from a Vehicle object into QML-accessible Q_PROPERTY values.
/// Provides a unified interface for all preflight checks to read vehicle state (battery, GPS,
/// RC, sensors, EKF, ESC, etc.) without depending on the QGC Vehicle API directly.

class Vehicle;

class TelemetryBridge : public QObject {
    Q_OBJECT

    // -- Connection status --
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(int connectionQuality READ connectionQuality NOTIFY connectionQualityChanged)

    // -- Battery / power --
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)
    Q_PROPERTY(double batteryCurrent READ batteryCurrent NOTIFY batteryCurrentChanged)
    Q_PROPERTY(double batteryTemperature READ batteryTemperature NOTIFY batteryTemperatureChanged)
    Q_PROPERTY(QVariantList batteryCellVoltages READ batteryCellVoltages NOTIFY batteryCellVoltagesChanged)
    Q_PROPERTY(double sysVoltageBattery READ sysVoltageBattery NOTIFY sysVoltageBatteryChanged)
    Q_PROPERTY(double battery2Voltage READ battery2Voltage NOTIFY battery2VoltageChanged)
    Q_PROPERTY(int battery2Percent READ battery2Percent NOTIFY battery2PercentChanged)
    Q_PROPERTY(double battery2Current READ battery2Current NOTIFY battery2CurrentChanged)
    Q_PROPERTY(bool battery2Present READ battery2Present NOTIFY battery2PresentChanged)
    Q_PROPERTY(int radioRssi READ radioRssi NOTIFY radioStatusChanged)
    Q_PROPERTY(int radioTxBuf READ radioTxBuf NOTIFY radioStatusChanged)
    Q_PROPERTY(int radioRxErrors READ radioRxErrors NOTIFY radioStatusChanged)
    Q_PROPERTY(double terrainHeight READ terrainHeight NOTIFY terrainReportChanged)
    Q_PROPERTY(bool companionDetected READ companionDetected NOTIFY companionDetectedChanged)
    Q_PROPERTY(uint8_t mavlinkVersion READ mavlinkVersion NOTIFY mavlinkVersionChanged)

    // -- GPS primary --
    Q_PROPERTY(int gpsFixType READ gpsFixType NOTIFY gpsFixTypeChanged)
    Q_PROPERTY(int gpsSatellites READ gpsSatellites NOTIFY gpsSatellitesChanged)
    Q_PROPERTY(double gpsLatitude READ gpsLatitude NOTIFY gpsPositionChanged)
    Q_PROPERTY(double gpsLongitude READ gpsLongitude NOTIFY gpsPositionChanged)
    Q_PROPERTY(double gpsHdop READ gpsHdop NOTIFY gpsHdopChanged)
    Q_PROPERTY(double gpsSpeedAccuracy READ gpsSpeedAccuracy NOTIFY gpsSpeedAccuracyChanged)
    Q_PROPERTY(double gpsAltitude READ gpsAltitude NOTIFY gpsAltitudeChanged)

    // -- GPS secondary --
    Q_PROPERTY(int gps2FixType READ gps2FixType NOTIFY gps2Changed)
    Q_PROPERTY(int gps2Satellites READ gps2Satellites NOTIFY gps2Changed)
    Q_PROPERTY(double gps2Latitude READ gps2Latitude NOTIFY gps2Changed)
    Q_PROPERTY(double gps2Longitude READ gps2Longitude NOTIFY gps2Changed)
    Q_PROPERTY(uint16_t gps2Eph READ gps2Eph NOTIFY gps2Changed)
    Q_PROPERTY(uint16_t gps2Yaw READ gps2Yaw NOTIFY gps2Changed)

    // -- Home position --
    Q_PROPERTY(double homeLatitude READ homeLatitude NOTIFY homePositionChanged)
    Q_PROPERTY(double homeLongitude READ homeLongitude NOTIFY homePositionChanged)
    Q_PROPERTY(double homeAltitude READ homeAltitude NOTIFY homePositionChanged)

    // -- IMU / compass / baro --
    Q_PROPERTY(bool imuHealthy READ imuHealthy NOTIFY imuHealthyChanged)
    Q_PROPERTY(bool compassHealthy READ compassHealthy NOTIFY compassHealthyChanged)
    Q_PROPERTY(double imuTemperature READ imuTemperature NOTIFY imuTemperatureChanged)
    Q_PROPERTY(double baroPressure READ baroPressure NOTIFY baroChanged)
    Q_PROPERTY(double baroTemperature READ baroTemperature NOTIFY baroChanged)

    // -- Airspeed / position --
    Q_PROPERTY(double airspeed READ airspeed NOTIFY airspeedChanged)
    Q_PROPERTY(double groundSpeed READ groundSpeed NOTIFY groundSpeedChanged)
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(double altitudeRelative READ altitudeRelative NOTIFY altitudeChanged)

    // -- RC link --
    Q_PROPERTY(int rcRssi READ rcRssi NOTIFY rcRssiChanged)
    Q_PROPERTY(bool rcConnected READ rcConnected NOTIFY rcConnectedChanged)
    Q_PROPERTY(bool rcFailsafe READ rcFailsafe NOTIFY rcFailsafeChanged)
    Q_PROPERTY(QVariantList rcChannelValues READ rcChannelValues NOTIFY rcChannelValuesChanged)
    Q_PROPERTY(qint64 rcLastUpdateUsec READ rcLastUpdateUsec NOTIFY rcLastUpdateChanged)

    // -- Arming / flight mode --
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(bool heartbeatReceived READ heartbeatReceived NOTIFY heartbeatReceivedChanged)

    // -- Vibration --
    Q_PROPERTY(double vibrationX READ vibrationX NOTIFY vibrationChanged)
    Q_PROPERTY(double vibrationY READ vibrationY NOTIFY vibrationChanged)
    Q_PROPERTY(double vibrationZ READ vibrationZ NOTIFY vibrationChanged)
    Q_PROPERTY(uint vibrationClipping READ vibrationClipping NOTIFY vibrationChanged)

    // -- EKF estimator status --
    Q_PROPERTY(uint estimatorFlags READ estimatorFlags NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorVelRatio READ estimatorVelRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorPosHorizRatio READ estimatorPosHorizRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorPosVertRatio READ estimatorPosVertRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorMagRatio READ estimatorMagRatio NOTIFY estimatorStatusChanged)

    // -- AHRS / sensor health --
    Q_PROPERTY(bool ahrsHealth READ ahrsHealth NOTIFY ahrsHealthChanged)
    Q_PROPERTY(uint sensorHealth READ sensorHealth NOTIFY sensorHealthChanged)
    Q_PROPERTY(double commDropRate READ commDropRate NOTIFY commDropRateChanged)

    // -- Wind --
    Q_PROPERTY(double windSpeed READ windSpeed NOTIFY windChanged)
    Q_PROPERTY(double windDirection READ windDirection NOTIFY windChanged)

    // -- Attitude / gyro --
    Q_PROPERTY(double gyroX READ gyroX NOTIFY gyroChanged)
    Q_PROPERTY(double gyroY READ gyroY NOTIFY gyroChanged)
    Q_PROPERTY(double gyroZ READ gyroZ NOTIFY gyroChanged)
    Q_PROPERTY(double roll READ roll NOTIFY attitudeChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY attitudeChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY attitudeChanged)
    Q_PROPERTY(double globalAltitude READ globalAltitude NOTIFY globalAltitudeChanged)

    // -- Optical flow / gimbal --
    Q_PROPERTY(int opticalFlowQuality READ opticalFlowQuality NOTIFY opticalFlowQualityChanged)
    Q_PROPERTY(bool gimbalDetected READ gimbalDetected NOTIFY gimbalDetectedChanged)
    Q_PROPERTY(int gimbalMode READ gimbalMode NOTIFY gimbalModeChanged)

    // -- EKF variances --
    Q_PROPERTY(double ekfVelVariance READ ekfVelVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfPosHorizVariance READ ekfPosHorizVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfPosVertVariance READ ekfPosVertVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfCompassVariance READ ekfCompassVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfTerrainVariance READ ekfTerrainVariance NOTIFY ekfVarianceChanged)

    // -- Pre-arm status --
    Q_PROPERTY(bool preArmOk READ preArmOk NOTIFY preArmOkChanged)
    Q_PROPERTY(QString preArmMessage READ preArmMessage NOTIFY preArmMessageChanged)

    // -- Motors --
    Q_PROPERTY(int motorCount READ motorCount NOTIFY motorCountChanged)
    Q_PROPERTY(QVariantList motorOutputs READ motorOutputs NOTIFY servoOutputsChanged)

    // -- Mission --
    Q_PROPERTY(int missionCount READ missionCount NOTIFY missionCountChanged)
    Q_PROPERTY(double missionFirstWpDistance READ missionFirstWpDistance NOTIFY missionFirstWpDistanceChanged)

    // -- Accelerometers --
    Q_PROPERTY(double accelerometerX READ accelerometerX NOTIFY accelerometerChanged)
    Q_PROPERTY(double accelerometerY READ accelerometerY NOTIFY accelerometerChanged)
    Q_PROPERTY(double accelerometerZ READ accelerometerZ NOTIFY accelerometerChanged)
    Q_PROPERTY(double accelerometer2X READ accelerometer2X NOTIFY accelerometer2Changed)
    Q_PROPERTY(double accelerometer2Y READ accelerometer2Y NOTIFY accelerometer2Changed)
    Q_PROPERTY(double accelerometer2Z READ accelerometer2Z NOTIFY accelerometer2Changed)

    // -- ESC telemetry --
    Q_PROPERTY(QVariantList escTemperatures READ escTemperatures NOTIFY escTelemetryChanged)
    Q_PROPERTY(QVariantList escVoltages READ escVoltages NOTIFY escTelemetryChanged)
    Q_PROPERTY(QVariantList escCurrents READ escCurrents NOTIFY escTelemetryChanged)
    Q_PROPERTY(QVariantList escRpm READ escRpm NOTIFY escTelemetryChanged)
    Q_PROPERTY(int escInfoCount READ escInfoCount NOTIFY escInfoChanged)
    Q_PROPERTY(QVariantList escInfoFailureFlags READ escInfoFailureFlags NOTIFY escInfoChanged)
    Q_PROPERTY(QVariantList escInfoErrorCount READ escInfoErrorCount NOTIFY escInfoChanged)
    Q_PROPERTY(int escInfoConnectionType READ escInfoConnectionType NOTIFY escInfoChanged)

    // -- Magnetic field --
    Q_PROPERTY(double magFieldX READ magFieldX NOTIFY magFieldChanged)
    Q_PROPERTY(double magFieldY READ magFieldY NOTIFY magFieldChanged)
    Q_PROPERTY(double magFieldZ READ magFieldZ NOTIFY magFieldChanged)

public:
    /// Construct a telemetry bridge.
    explicit TelemetryBridge(QObject *parent = nullptr);

    /// Set the Vehicle to bridge telemetry from.
    void setVehicle(Vehicle* vehicle);
    /// Currently bridged vehicle.
    Vehicle* vehicle() const { return _vehicle; }

    /// Whether the vehicle is currently connected.
    virtual bool isConnected() const;
    virtual int connectionQuality() const { return _connectionQuality; }

    double batteryVoltage() const { return _batteryVoltage; }
    int batteryPercent() const { return _batteryPercent; }
    double batteryCurrent() const { return _batteryCurrent; }
    double batteryTemperature() const { return _batteryTemperature; }
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

    int missionCount() const { return _missionCount; }
    double missionFirstWpDistance() const { return _missionFirstWpDistance; }

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

    /// Set a MAVLink parameter value on the vehicle.
    Q_INVOKABLE void setParameterValue(const QString& name, float value);
    /// Check whether a parameter exists in the local cache.
    Q_INVOKABLE bool hasParameter(const QString& name) const;
    /// Read a parameter value from the local cache (with default fallback).
    Q_INVOKABLE float parameterValue(const QString& name, float defaultValue = 0.0f) const;
    /// Whether the parameter cache has been fully loaded.
    bool parametersReady() const { return _parametersReady; }

signals:
    /// Emitted when the parameter cache ready state changes.
    void parametersReadyChanged(bool ready);
    void isConnectedChanged();
    void connectionQualityChanged();
    void batteryVoltageChanged();
    void batteryPercentChanged();
    void batteryCurrentChanged();
    void batteryTemperatureChanged();
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
    void accelerometerChanged();
    void accelerometer2Changed();
    void escTelemetryChanged();
    void escInfoChanged();
    void magFieldChanged();

private slots:
    void _onVehicleConnectedChanged(bool connected);
    void _onParameterReadyChanged(bool ready);
    void _onTelemetryUpdate();
    void _handleMavlinkMessage(const mavlink_message_t& message);

private:
    void _connectVehicleSignals();
    void _connectBatteryFacts(QObject* batteryModel);
    void _loadParameters();
    void _updateFromVehicle();
    void _updateMissionInfo();

    Vehicle* _vehicle = nullptr;
    int _connectionQuality = 0;
    QTimer _updateTimer;

    double _batteryVoltage = 0.0;
    int _batteryPercent = 0;
    double _batteryCurrent = 0.0;
    double _batteryTemperature = qQNaN();
    QVariantList _batteryCellVoltages;
    double _sysVoltageBattery = 0.0;
    double _battery2Voltage = 0.0;
    int _battery2Percent = -1;
    double _battery2Current = 0.0;
    bool _battery2Present = false;
    int _radioRssi = 0;
    int _radioTxBuf = 0;
    int _radioRxErrors = 0;
    double _terrainHeight = qQNaN();
    bool _companionDetected = false;
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

    int _missionCount = 0;
    double _missionFirstWpDistance = -1.0;

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

    QMap<QString, float> _parameterCache;
    bool _parametersReady = false;
};
