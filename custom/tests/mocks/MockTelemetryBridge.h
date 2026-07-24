#pragma once

#include "TelemetryBridge.h"

class MockTelemetryBridge : public TelemetryBridge {
    Q_OBJECT
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(int connectionQuality READ connectionQuality NOTIFY connectionQualityChanged)
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)
    Q_PROPERTY(double batteryCurrent READ batteryCurrent NOTIFY batteryCurrentChanged)
    Q_PROPERTY(double batteryTemperature READ batteryTemperature NOTIFY batteryTemperatureChanged)
    Q_PROPERTY(double sysVoltageBattery READ sysVoltageBattery NOTIFY sysVoltageBatteryChanged)
    Q_PROPERTY(int gpsFixType READ gpsFixType NOTIFY gpsFixTypeChanged)
    Q_PROPERTY(int gpsSatellites READ gpsSatellites NOTIFY gpsSatellitesChanged)
    Q_PROPERTY(double gpsHdop READ gpsHdop NOTIFY gpsHdopChanged)
    Q_PROPERTY(double gpsLatitude READ gpsLatitude NOTIFY gpsPositionChanged)
    Q_PROPERTY(double gpsLongitude READ gpsLongitude NOTIFY gpsPositionChanged)
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(bool rcConnected READ rcConnected NOTIFY rcConnectedChanged)
    Q_PROPERTY(int rcRssi READ rcRssi NOTIFY rcRssiChanged)
    Q_PROPERTY(bool rcFailsafe READ rcFailsafe NOTIFY rcFailsafeChanged)
    Q_PROPERTY(bool imuHealthy READ imuHealthy NOTIFY imuHealthyChanged)
    Q_PROPERTY(bool compassHealthy READ compassHealthy NOTIFY compassHealthyChanged)
    Q_PROPERTY(double imuTemperature READ imuTemperature NOTIFY imuTemperatureChanged)
    Q_PROPERTY(double baroPressure READ baroPressure NOTIFY baroChanged)
    Q_PROPERTY(double baroTemperature READ baroTemperature NOTIFY baroChanged)
    Q_PROPERTY(double airspeed READ airspeed NOTIFY airspeedChanged)
    Q_PROPERTY(double groundSpeed READ groundSpeed NOTIFY groundSpeedChanged)
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(double altitudeRelative READ altitudeRelative NOTIFY altitudeChanged)
    Q_PROPERTY(double globalAltitude READ globalAltitude NOTIFY globalAltitudeChanged)
    Q_PROPERTY(double roll READ roll NOTIFY attitudeChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY attitudeChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY attitudeChanged)
    Q_PROPERTY(double vibrationX READ vibrationX NOTIFY vibrationChanged)
    Q_PROPERTY(double vibrationY READ vibrationY NOTIFY vibrationChanged)
    Q_PROPERTY(double vibrationZ READ vibrationZ NOTIFY vibrationChanged)
    Q_PROPERTY(uint vibrationClipping READ vibrationClipping NOTIFY vibrationChanged)
    Q_PROPERTY(uint estimatorFlags READ estimatorFlags NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorVelRatio READ estimatorVelRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorPosHorizRatio READ estimatorPosHorizRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorPosVertRatio READ estimatorPosVertRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(double estimatorMagRatio READ estimatorMagRatio NOTIFY estimatorStatusChanged)
    Q_PROPERTY(bool ahrsHealth READ ahrsHealth NOTIFY ahrsHealthChanged)
    Q_PROPERTY(double windSpeed READ windSpeed NOTIFY windChanged)
    Q_PROPERTY(double windDirection READ windDirection NOTIFY windChanged)
    Q_PROPERTY(double gyroX READ gyroX NOTIFY gyroChanged)
    Q_PROPERTY(double gyroY READ gyroY NOTIFY gyroChanged)
    Q_PROPERTY(double gyroZ READ gyroZ NOTIFY gyroChanged)
    Q_PROPERTY(bool preArmOk READ preArmOk NOTIFY preArmOkChanged)
    Q_PROPERTY(int motorCount READ motorCount NOTIFY motorCountChanged)
    Q_PROPERTY(int missionCount READ missionCount NOTIFY missionCountChanged)
    Q_PROPERTY(double missionTotalDistance READ missionTotalDistance NOTIFY missionTotalDistanceChanged)
    Q_PROPERTY(int escInfoCount READ escInfoCount NOTIFY escInfoChanged)
    Q_PROPERTY(double magFieldX READ magFieldX NOTIFY magFieldChanged)
    Q_PROPERTY(double magFieldY READ magFieldY NOTIFY magFieldChanged)
    Q_PROPERTY(double magFieldZ READ magFieldZ NOTIFY magFieldChanged)
    Q_PROPERTY(int opticalFlowQuality READ opticalFlowQuality NOTIFY opticalFlowQualityChanged)
    Q_PROPERTY(bool gimbalDetected READ gimbalDetected NOTIFY gimbalDetectedChanged)
    Q_PROPERTY(double commDropRate READ commDropRate NOTIFY commDropRateChanged)
    Q_PROPERTY(bool companionDetected READ companionDetected NOTIFY companionDetectedChanged)
    Q_PROPERTY(bool preArmOk READ preArmOk NOTIFY preArmOkChanged)
    Q_PROPERTY(double ekfVelVariance READ ekfVelVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfPosHorizVariance READ ekfPosHorizVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfPosVertVariance READ ekfPosVertVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfCompassVariance READ ekfCompassVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(double ekfTerrainVariance READ ekfTerrainVariance NOTIFY ekfVarianceChanged)
    Q_PROPERTY(QVariantList rcChannelValues READ rcChannelValues NOTIFY rcChannelValuesChanged)
    Q_PROPERTY(qint64 rcLastUpdateUsec READ rcLastUpdateUsec NOTIFY rcLastUpdateChanged)

public:
    explicit MockTelemetryBridge(QObject *parent = nullptr)
        : TelemetryBridge(parent) {}

    // -- Setters that emit NOTIFY signals --
    void setConnected(bool v) { m_connected = v; emit isConnectedChanged(); }
    void setConnectionQuality(int v) { m_connectionQuality = v; emit connectionQualityChanged(); }
    void setBatteryVoltage(double v) { m_batteryVoltage = v; emit batteryVoltageChanged(); }
    void setBatteryPercent(int v) { m_batteryPercent = v; emit batteryPercentChanged(); }
    void setBatteryCurrent(double v) { m_batteryCurrent = v; emit batteryCurrentChanged(); }
    void setBatteryTemperature(double v) { m_batteryTemperature = v; emit batteryTemperatureChanged(); }
    void setSysVoltageBattery(double v) { m_sysVoltageBattery = v; emit sysVoltageBatteryChanged(); }
    void setGpsFixType(int v) { m_gpsFixType = v; emit gpsFixTypeChanged(); }
    void setGpsSatellites(int v) { m_gpsSatellites = v; emit gpsSatellitesChanged(); }
    void setGpsHdop(double v) { m_gpsHdop = v; emit gpsHdopChanged(); }
    void setGpsPosition(double lat, double lon) {
        m_gpsLatitude = lat; m_gpsLongitude = lon;
        emit gpsPositionChanged();
    }
    void setArmed(bool v) { m_armed = v; emit armedChanged(); }
    void setRcConnected(bool v) { m_rcConnected = v; emit rcConnectedChanged(); }
    void setRcRssi(int v) { m_rcRssi = v; emit rcRssiChanged(); }
    void setRcFailsafe(bool v) { m_rcFailsafe = v; emit rcFailsafeChanged(); }
    void setImuHealthy(bool v) { m_imuHealthy = v; emit imuHealthyChanged(); }
    void setCompassHealthy(bool v) { m_compassHealthy = v; emit compassHealthyChanged(); }
    void setImuTemperature(double v) { m_imuTemperature = v; emit imuTemperatureChanged(); }
    void setBaroPressure(double v) { m_baroPressure = v; emit baroChanged(); }
    void setBaroTemperature(double v) { m_baroTemperature = v; emit baroChanged(); }
    void setAirspeed(double v) { m_airspeed = v; emit airspeedChanged(); }
    void setGroundSpeed(double v) { m_groundSpeed = v; emit groundSpeedChanged(); }
    void setHeading(double v) { m_heading = v; emit headingChanged(); }
    void setAltitudeRelative(double v) { m_altitudeRelative = v; emit altitudeChanged(); }
    void setGlobalAltitude(double v) { m_globalAltitude = v; emit globalAltitudeChanged(); }
    void setAttitude(double r, double p, double y) {
        m_roll = r; m_pitch = p; m_yaw = y;
        emit attitudeChanged();
    }
    void setVibration(double x, double y, double z, uint clip) {
        m_vibrationX = x; m_vibrationY = y; m_vibrationZ = z; m_vibrationClipping = clip;
        emit vibrationChanged();
    }
    void setEstimatorStatus(uint flags, double velRatio, double posHorizRatio,
                            double posVertRatio, double magRatio) {
        m_estimatorFlags = flags;
        m_estimatorVelRatio = velRatio;
        m_estimatorPosHorizRatio = posHorizRatio;
        m_estimatorPosVertRatio = posVertRatio;
        m_estimatorMagRatio = magRatio;
        emit estimatorStatusChanged();
    }
    void setAhrsHealth(bool v) { m_ahrsHealth = v; emit ahrsHealthChanged(); }
    void setWind(double speed, double dir) { m_windSpeed = speed; m_windDirection = dir; emit windChanged(); }
    void setGyro(double x, double y, double z) { m_gyroX = x; m_gyroY = y; m_gyroZ = z; emit gyroChanged(); }
    void setPreArmOk(bool v) { m_preArmOk = v; emit preArmOkChanged(); }
    void setMotorCount(int v) { m_motorCount = v; emit motorCountChanged(); }
    void setMissionCount(int v) { m_missionCount = v; emit missionCountChanged(); }
    void setMissionTotalDistance(double v) { m_missionTotalDistance = v; emit missionTotalDistanceChanged(); }
    void setEscInfoCount(int v) { m_escInfoCount = v; emit escInfoChanged(); }
    void setMagField(double x, double y, double z) {
        m_magFieldX = x; m_magFieldY = y; m_magFieldZ = z;
        emit magFieldChanged();
    }
    void setOpticalFlowQuality(int v) { m_opticalFlowQuality = v; emit opticalFlowQualityChanged(); }
    void setGimbalDetected(bool v) { m_gimbalDetected = v; emit gimbalDetectedChanged(); }
    void setCommDropRate(double v) { m_commDropRate = v; emit commDropRateChanged(); }
    void setCompanionDetected(bool v) { m_companionDetected = v; emit companionDetectedChanged(); }
    void setEkfVariance(double vel, double posH, double posV, double compass, double terrain) {
        m_ekfVelVariance = vel; m_ekfPosHorizVariance = posH;
        m_ekfPosVertVariance = posV; m_ekfCompassVariance = compass; m_ekfTerrainVariance = terrain;
        emit ekfVarianceChanged();
    }
    void setRcChannelValues(const QVariantList &v) { m_rcChannelValues = v; emit rcChannelValuesChanged(); }
    void setRcLastUpdateUsec(qint64 v) { m_rcLastUpdateUsec = v; emit rcLastUpdateChanged(); }
    void setVehicleType(const QString &v) { m_vehicleType = v; emit vehicleTypeChanged(); }
    void setConnectionStatus(const QString &v) { m_connectionStatus = v; emit connectionStatusChanged(); }
    void setConnectionUrl(const QString &v) { m_connectionUrl = v; emit connectionUrlChanged(); }
    void setAutopilotType(const QString &v) { m_autopilotType = v; emit autopilotTypeChanged(); }
    void setGpsFixTypeString(const QString &v) { m_gpsFixTypeString = v; emit gpsFixTypeChanged(); }
    void setGpsDataQuality(int v) { m_gpsDataQuality = v; emit gpsDataQualityChanged(); }
    void setGpsStatusString(const QString &v) { m_gpsStatusString = v; emit gpsDataQualityChanged(); }
    void setBatteryDataQuality(int v) { m_batteryDataQuality = v; emit batteryDataQualityChanged(); }
    void setBatteryStatusString(const QString &v) { m_batteryStatusString = v; emit batteryDataQualityChanged(); }
    void setServoOutputsString(const QString &v) { m_servoOutputsString = v; emit servoOutputsChanged(); }
    void setGimbalAttitude(double pitch, double roll, double yaw) {
        m_gimbalPitch = pitch; m_gimbalRoll = roll; m_gimbalYaw = yaw;
        emit gimbalAttitudeChanged();
    }
    void setGimbalCalibrating(bool v) { m_gimbalCalibrating = v; emit gimbalCalibratingChanged(); }
    void setEkfAirspeedVariance(double v) { m_ekfAirspeedVariance = v; emit ekfVarianceChanged(); }
    void setEkfStatus(const QString &v) { m_ekfStatus = v; emit ekfStatusChanged(); }
    void setFlightTime(double v) { m_flightTime = v; emit flightTimeChanged(); }
    void setLastLogTimestamp(const QString &v) { m_lastLogTimestamp = v; emit lastLogTimestampChanged(); }
    void setPreArmSeverity(const QString &v) { m_preArmSeverity = v; emit preArmSeverityChanged(); }
    void setImuDataQuality(int v) { m_imuDataQuality = v; emit imuDataQualityChanged(); }
    void setCompassDataQuality(int v) { m_compassDataQuality = v; emit compassDataQualityChanged(); }
    void setRcDataQuality(int v) { m_rcDataQuality = v; emit rcDataQualityChanged(); }
    void setHardwareSetupRequired(bool v) { m_hardwareSetupRequired = v; emit hardwareSetupRequiredChanged(); }

    // -- Override TelemetryBridge virtuals --
    bool isConnected() const override { return m_connected; }
    int connectionQuality() const override { return m_connectionQuality; }

    // -- Getters (return mock values) --
    bool armed() const { return m_armed; }
    double batteryVoltage() const { return m_batteryVoltage; }
    int batteryPercent() const { return m_batteryPercent; }
    double batteryCurrent() const { return m_batteryCurrent; }
    double batteryTemperature() const { return m_batteryTemperature; }
    double sysVoltageBattery() const { return m_sysVoltageBattery; }
    int gpsFixType() const { return m_gpsFixType; }
    int gpsSatellites() const { return m_gpsSatellites; }
    double gpsHdop() const { return m_gpsHdop; }
    double gpsLatitude() const { return m_gpsLatitude; }
    double gpsLongitude() const { return m_gpsLongitude; }
    bool rcConnected() const { return m_rcConnected; }
    int rcRssi() const { return m_rcRssi; }
    bool rcFailsafe() const { return m_rcFailsafe; }
    bool imuHealthy() const { return m_imuHealthy; }
    bool compassHealthy() const { return m_compassHealthy; }
    double imuTemperature() const { return m_imuTemperature; }
    double baroPressure() const { return m_baroPressure; }
    double baroTemperature() const { return m_baroTemperature; }
    double airspeed() const { return m_airspeed; }
    double groundSpeed() const { return m_groundSpeed; }
    double heading() const { return m_heading; }
    double altitudeRelative() const { return m_altitudeRelative; }
    double globalAltitude() const { return m_globalAltitude; }
    double roll() const { return m_roll; }
    double pitch() const { return m_pitch; }
    double yaw() const { return m_yaw; }
    double vibrationX() const { return m_vibrationX; }
    double vibrationY() const { return m_vibrationY; }
    double vibrationZ() const { return m_vibrationZ; }
    uint vibrationClipping() const { return m_vibrationClipping; }
    uint estimatorFlags() const { return m_estimatorFlags; }
    double estimatorVelRatio() const { return m_estimatorVelRatio; }
    double estimatorPosHorizRatio() const { return m_estimatorPosHorizRatio; }
    double estimatorPosVertRatio() const { return m_estimatorPosVertRatio; }
    double estimatorMagRatio() const { return m_estimatorMagRatio; }
    bool ahrsHealth() const { return m_ahrsHealth; }
    uint sensorHealth() const { return m_sensorHealth; }
    double windSpeed() const { return m_windSpeed; }
    double windDirection() const { return m_windDirection; }
    double gyroX() const { return m_gyroX; }
    double gyroY() const { return m_gyroY; }
    double gyroZ() const { return m_gyroZ; }
    bool preArmOk() const { return m_preArmOk; }
    int motorCount() const { return m_motorCount; }
    int missionCount() const { return m_missionCount; }
    double missionTotalDistance() const { return m_missionTotalDistance; }
    int escInfoCount() const { return m_escInfoCount; }
    double magFieldX() const { return m_magFieldX; }
    double magFieldY() const { return m_magFieldY; }
    double magFieldZ() const { return m_magFieldZ; }
    int opticalFlowQuality() const { return m_opticalFlowQuality; }
    bool gimbalDetected() const { return m_gimbalDetected; }
    double commDropRate() const { return m_commDropRate; }
    bool companionDetected() const { return m_companionDetected; }
    double ekfVelVariance() const { return m_ekfVelVariance; }
    double ekfPosHorizVariance() const { return m_ekfPosHorizVariance; }
    double ekfPosVertVariance() const { return m_ekfPosVertVariance; }
    double ekfCompassVariance() const { return m_ekfCompassVariance; }
    double ekfTerrainVariance() const { return m_ekfTerrainVariance; }
    QVariantList rcChannelValues() const { return m_rcChannelValues; }
    qint64 rcLastUpdateUsec() const { return m_rcLastUpdateUsec; }
    QString vehicleType() const { return m_vehicleType; }
    QString connectionStatus() const { return m_connectionStatus; }
    QString connectionUrl() const { return m_connectionUrl; }
    QString autopilotType() const { return m_autopilotType; }
    QString gpsFixTypeString() const { return m_gpsFixTypeString; }
    int gpsDataQuality() const { return m_gpsDataQuality; }
    QString gpsStatusString() const { return m_gpsStatusString; }
    int batteryDataQuality() const { return m_batteryDataQuality; }
    QString batteryStatusString() const { return m_batteryStatusString; }
    QString servoOutputsString() const { return m_servoOutputsString; }
    double gimbalPitch() const { return m_gimbalPitch; }
    double gimbalRoll() const { return m_gimbalRoll; }
    double gimbalYaw() const { return m_gimbalYaw; }
    bool gimbalCalibrating() const { return m_gimbalCalibrating; }
    double ekfAirspeedVariance() const { return m_ekfAirspeedVariance; }
    QString ekfStatus() const { return m_ekfStatus; }
    double flightTime() const { return m_flightTime; }
    QString lastLogTimestamp() const { return m_lastLogTimestamp; }
    QString preArmSeverity() const { return m_preArmSeverity; }
    int imuDataQuality() const { return m_imuDataQuality; }
    int compassDataQuality() const { return m_compassDataQuality; }
    int rcDataQuality() const { return m_rcDataQuality; }
    bool hardwareSetupRequired() const { return m_hardwareSetupRequired; }

signals:
    void isConnectedChanged();
    void connectionQualityChanged();
    void batteryVoltageChanged();
    void batteryPercentChanged();
    void batteryCurrentChanged();
    void batteryTemperatureChanged();
    void sysVoltageBatteryChanged();
    void gpsFixTypeChanged();
    void gpsSatellitesChanged();
    void gpsHdopChanged();
    void gpsPositionChanged();
    void armedChanged();
    void rcConnectedChanged();
    void rcRssiChanged();
    void rcFailsafeChanged();
    void imuHealthyChanged();
    void compassHealthyChanged();
    void imuTemperatureChanged();
    void baroChanged();
    void airspeedChanged();
    void groundSpeedChanged();
    void headingChanged();
    void altitudeChanged();
    void globalAltitudeChanged();
    void attitudeChanged();
    void vibrationChanged();
    void estimatorStatusChanged();
    void ahrsHealthChanged();
    void windChanged();
    void gyroChanged();
    void preArmOkChanged();
    void motorCountChanged();
    void missionCountChanged();
    void missionTotalDistanceChanged();
    void escInfoChanged();
    void magFieldChanged();
    void opticalFlowQualityChanged();
    void gimbalDetectedChanged();
    void commDropRateChanged();
    void companionDetectedChanged();
    void ekfVarianceChanged();
    void rcChannelValuesChanged();
    void rcLastUpdateChanged();
    void vehicleTypeChanged();
    void connectionStatusChanged();
    void connectionUrlChanged();
    void autopilotTypeChanged();
    void gpsDataQualityChanged();
    void batteryDataQualityChanged();
    void servoOutputsChanged();
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

private:
    bool m_connected = false;
    int m_connectionQuality = 0;
    double m_batteryVoltage = 0.0;
    int m_batteryPercent = 0;
    double m_batteryCurrent = 0.0;
    double m_batteryTemperature = qQNaN();
    double m_sysVoltageBattery = 0.0;
    int m_gpsFixType = 0;
    int m_gpsSatellites = 0;
    double m_gpsHdop = -1.0;
    double m_gpsLatitude = 0.0;
    double m_gpsLongitude = 0.0;
    bool m_armed = false;
    bool m_rcConnected = false;
    int m_rcRssi = 0;
    bool m_rcFailsafe = false;
    bool m_imuHealthy = false;
    bool m_compassHealthy = false;
    double m_imuTemperature = qQNaN();
    double m_baroPressure = qQNaN();
    double m_baroTemperature = qQNaN();
    double m_airspeed = 0.0;
    double m_groundSpeed = 0.0;
    double m_heading = 0.0;
    double m_altitudeRelative = 0.0;
    double m_globalAltitude = qQNaN();
    double m_roll = 0.0;
    double m_pitch = 0.0;
    double m_yaw = 0.0;
    double m_vibrationX = 0.0;
    double m_vibrationY = 0.0;
    double m_vibrationZ = 0.0;
    uint m_vibrationClipping = 0;
    uint m_estimatorFlags = 0;
    double m_estimatorVelRatio = 0.0;
    double m_estimatorPosHorizRatio = 0.0;
    double m_estimatorPosVertRatio = 0.0;
    double m_estimatorMagRatio = 0.0;
    bool m_ahrsHealth = false;
    uint m_sensorHealth = 0;
    double m_windSpeed = 0.0;
    double m_windDirection = 0.0;
    double m_gyroX = qQNaN();
    double m_gyroY = qQNaN();
    double m_gyroZ = qQNaN();
    bool m_preArmOk = false;
    int m_motorCount = 0;
    int m_missionCount = 0;
    double m_missionTotalDistance = -1.0;
    int m_escInfoCount = 0;
    double m_magFieldX = qQNaN();
    double m_magFieldY = qQNaN();
    double m_magFieldZ = qQNaN();
    int m_opticalFlowQuality = -1;
    bool m_gimbalDetected = false;
    double m_commDropRate = 0.0;
    bool m_companionDetected = false;
    double m_ekfVelVariance = 0.0;
    double m_ekfPosHorizVariance = 0.0;
    double m_ekfPosVertVariance = 0.0;
    double m_ekfCompassVariance = 0.0;
    double m_ekfTerrainVariance = 0.0;
    QVariantList m_rcChannelValues;
    qint64 m_rcLastUpdateUsec = 0;
    QString m_vehicleType;
    QString m_connectionStatus;
    QString m_connectionUrl;
    QString m_autopilotType;
    QString m_gpsFixTypeString;
    int m_gpsDataQuality = 2;
    QString m_gpsStatusString;
    int m_batteryDataQuality = 2;
    QString m_batteryStatusString;
    QString m_servoOutputsString;
    double m_gimbalPitch = 0.0;
    double m_gimbalRoll = 0.0;
    double m_gimbalYaw = 0.0;
    bool m_gimbalCalibrating = false;
    double m_ekfAirspeedVariance = 0.0;
    QString m_ekfStatus;
    double m_flightTime = 0.0;
    QString m_lastLogTimestamp;
    QString m_preArmSeverity;
    int m_imuDataQuality = 3;
    int m_compassDataQuality = 3;
    int m_rcDataQuality = 2;
    bool m_hardwareSetupRequired = false;
};
