#include "AhrsHealthCheck.h"

#include "TelemetryBridge.h"

AhrsHealthCheck::AhrsHealthCheck(TelemetryBridge *telemetry,
                                 QObject *parent)
    : AbstractCheck(QStringLiteral("nav.imu.ahrs_health"),
                    QStringLiteral("AHRS Health"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Warn if attitude stabilization bit is clear or ahrsHealth is false; pass if healthy.
void AhrsHealthCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Check SYS_STATUS sensor health bitmask: attitude stabilization (bit 15)
    QVariant sensorVar = getTelemetryVariant(QStringLiteral("sensorHealth"));
    if (sensorVar.isValid()) {
        uint32_t health = sensorVar.toUInt();
        if (!(health & MAV_SYS_STATUS_SENSOR_ATTITUDE_STABILIZATION)) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Attitude stabilization unhealthy — AHRS may be degraded"));
            return;
        }
    }

    QVariant healthVar = getTelemetryVariant(QStringLiteral("ahrsHealth"));
    if (!healthVar.isValid()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No AHRS health data yet"));
        return;
    }

    bool healthy = healthVar.toBool();
    if (healthy) {
        setStatus(CheckStatus::Passed, QStringLiteral("AHRS healthy"));
    } else {
        setStatus(CheckStatus::Warning, QStringLiteral("AHRS unhealthy — expect degraded navigation"));
    }
}
