#include "BaroTemperatureCheck.h"

#include "TelemetryBridge.h"

BaroTemperatureCheck::BaroTemperatureCheck(TelemetryBridge *telemetry,
                                           double maxTempC, double minTempC,
                                           QObject *parent)
    : AbstractCheck(QStringLiteral("nav.baro.temperature"),
                    QStringLiteral("Barometer Temperature"),
                    CheckCategory::Navigation, CheckType::Auto, false, true, parent)
    , m_maxTempC(maxTempC)
    , m_minTempC(minTempC)
{
    m_telemetry = telemetry;
}

void BaroTemperatureCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant tempVar = getTelemetryVariant(QStringLiteral("baroTemperature"));
    if (!tempVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No barometer data"));
        return;
    }

    double temp = tempVar.toDouble();
    if (qIsNaN(temp)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No barometer temperature sensor"));
        return;
    }

    // Pass if baro temp is within [minTempC, maxTempC]; warn if outside range.
    setCurrentValue(temp);

    if (temp >= m_minTempC && temp <= m_maxTempC) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Baro temperature: %1°C — within range")
                      .arg(temp, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Baro temperature: %1°C — outside %2°C..%3°C range")
                      .arg(temp, 0, 'f', 1).arg(m_minTempC, 0, 'f', 1).arg(m_maxTempC, 0, 'f', 1));
    }
}

