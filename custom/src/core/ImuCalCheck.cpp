#include "ImuCalCheck.h"

#include "TelemetryBridge.h"

ImuCalCheck::ImuCalCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("airframe.imu.cal"),
                    QStringLiteral("IMU Calibration"),
                    CheckCategory::Airframe, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void ImuCalCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Check SYS_STATUS sensor health bitmask: gyro (bit 0) and accel (bit 1)
    QVariant healthVar = getTelemetryVariant(QStringLiteral("sensorHealth"));
    if (healthVar.isValid()) {
        uint32_t health = healthVar.toUInt();
        bool gyroOk = health & MAV_SYS_STATUS_SENSOR_3D_GYRO;
        bool accelOk = health & MAV_SYS_STATUS_SENSOR_3D_ACCEL;

        if (!gyroOk && !accelOk) {
            setStatus(CheckStatus::Failed, QStringLiteral("Gyro and accel both unhealthy"));
            return;
        }
        if (!gyroOk) {
            setStatus(CheckStatus::Warning, QStringLiteral("Gyro sensor unhealthy"));
            return;
        }
        if (!accelOk) {
            setStatus(CheckStatus::Warning, QStringLiteral("Accelerometer unhealthy"));
            return;
        }
    }

    bool healthy = getTelemetryBool(QStringLiteral("imuHealthy"));
    setCurrentValue(healthy);

    if (!healthy) {
        setStatus(CheckStatus::Warning, QStringLiteral("IMU unhealthy — sensor may be initializing"));
        return;
    }

    // Pass if gyro/accel health bits are set, imuHealthy is true, and quality >= 2.
    QVariant qualityVar = getTelemetryVariant(QStringLiteral("imuDataQuality"));
    if (!qualityVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("IMU data quality not reported"));
        return;
    }

    int quality = qualityVar.toInt();
    if (quality < 2) {
        setStatus(CheckStatus::Warning, QStringLiteral("IMU data quality low"));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Gyro & accel calibrated — quality %1").arg(quality));
}
