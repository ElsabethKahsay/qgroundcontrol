#include "BatteryTemperatureCheck.h"

#include "TelemetryBridge.h"

BatteryTemperatureCheck::BatteryTemperatureCheck(TelemetryBridge *telemetry,
                                                 double maxTemp,
                                                 double minTemp,
                                                 QObject *parent)
    : AbstractCheck(QStringLiteral("power.battery.temperature"),
                    QStringLiteral("Battery Temperature"),
                    CheckCategory::Power, CheckType::Auto, true, false, parent)
    , m_maxTemp(maxTemp)
    , m_minTemp(minTemp)
{
    m_telemetry = telemetry;
}

// Fail if averaged temperature exceeds max or falls below min; otherwise pass.
void BatteryTemperatureCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double rawTemp = getTelemetryDouble(QStringLiteral("batteryTemperature"));

    if (qIsNaN(rawTemp)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Battery temperature unknown"));
        return;
    }

    // 3-second moving average for hysteresis
    m_avgFilter.addSample(rawTemp);
    double temp = m_avgFilter.average();

    setCurrentValue(temp);

    if (temp > m_maxTemp) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Battery temperature %1\u00b0C exceeds max %2\u00b0C (avg %3s)")
                      .arg(temp, 0, 'f', 1).arg(m_maxTemp, 0, 'f', 1).arg(3));
    } else if (temp < m_minTemp) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Battery temperature %1\u00b0C below min %2\u00b0C (avg %3s)")
                      .arg(temp, 0, 'f', 1).arg(m_minTemp, 0, 'f', 1).arg(3));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Battery temperature %1\u00b0C (avg %2s)")
                      .arg(temp, 0, 'f', 1).arg(3));
    }
}
