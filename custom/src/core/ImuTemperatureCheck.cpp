#include "ImuTemperatureCheck.h"

#include "TelemetryBridge.h"

ImuTemperatureCheck::ImuTemperatureCheck(TelemetryBridge *telemetry,
                                         double maxTemp, double minTemp,
                                         QObject *parent)
    : AbstractCheck(QStringLiteral("nav.imu.temperature"),
                    QStringLiteral("IMU Temperature"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxTemp(maxTemp)
    , m_minTemp(minTemp)
{
    m_telemetry = telemetry;
}

void ImuTemperatureCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant tempVar = getTelemetryVariant(QStringLiteral("imuTemperature"));
    if (!tempVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("IMU temperature not reported by sensor"));
        return;
    }

    double temp = tempVar.toDouble();
    if (qIsNaN(temp)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("IMU temperature not reported by sensor"));
        return;
    }

    if (temp > m_maxTemp) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("IMU temperature %1°C exceeds max %2°C")
                      .arg(temp, 0, 'f', 1).arg(m_maxTemp, 0, 'f', 1));
    } else if (temp < m_minTemp) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("IMU temperature %1°C below min %2°C")
                      .arg(temp, 0, 'f', 1).arg(m_minTemp, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("IMU temperature %1°C").arg(temp, 0, 'f', 1));
    }
}
