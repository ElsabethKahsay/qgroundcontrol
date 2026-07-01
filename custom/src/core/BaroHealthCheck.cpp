#include "BaroHealthCheck.h"

#include "TelemetryBridge.h"

BaroHealthCheck::BaroHealthCheck(TelemetryBridge *telemetry,
                                 QObject *parent)
    : AbstractCheck(QStringLiteral("nav.baro.health"),
                    QStringLiteral("Barometer Health"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void BaroHealthCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Check SYS_STATUS sensor health bitmask: absolute pressure (bit 3 = 0x08)
    QVariant healthVar = getTelemetryVariant(QStringLiteral("sensorHealth"));
    if (healthVar.isValid()) {
        uint32_t health = healthVar.toUInt();
        if (!(health & MAV_SYS_STATUS_SENSOR_ABSOLUTE_PRESSURE)) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Barometer unhealthy per SYS_STATUS — pressure sensor fault"));
            return;
        }
    }

    double baroPress = getTelemetryDouble(QStringLiteral("baroPressure"));
    double baroTemp = getTelemetryDouble(QStringLiteral("baroTemperature"));

    if (qIsNaN(baroPress) || baroPress <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for baro data"));
        return;
    }

    if (!qIsNaN(baroTemp) && (baroTemp < -40.0 || baroTemp > 85.0)) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Baro temperature %1°C out of range")
                      .arg(baroTemp, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Baro %1 hPa — healthy")
                  .arg(baroPress, 0, 'f', 1));
}

QString BaroHealthCheck::getRationale() const
{
    return QStringLiteral("The barometer provides critical altitude data for EKF estimation "
                          "and altitude hold. A failed barometer prevents reliable altitude control.");
}

QStringList BaroHealthCheck::getFixSteps() const
{
    return {
        QStringLiteral("Check barometer sensor vent hole is not blocked or covered"),
        QStringLiteral("Verify baro sensor is enabled (BARO_ENABLE parameter)"),
        QStringLiteral("Ensure baro sensor is not exposed to direct sunlight or heat sources"),
        QStringLiteral("Cycle autopilot power to reset the sensor")
    };
}
