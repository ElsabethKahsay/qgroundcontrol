#include "AmbientTemperatureCheck.h"
#include "TelemetryBridge.h"

AmbientTemperatureCheck::AmbientTemperatureCheck(TelemetryBridge *telemetry,
                                                 double maxTempC, double minTempC,
                                                 QObject *parent)
    : AbstractCheck(QStringLiteral("environment.ambient_temperature"),
                    QStringLiteral("Ambient Temperature"),
                    CheckCategory::Environment, CheckType::Auto, false, true, parent)
    , m_maxTempC(maxTempC)
    , m_minTempC(minTempC)
{
    m_telemetry = telemetry;
}

void AmbientTemperatureCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant tempVar = getTelemetryVariant(QStringLiteral("baroTemperature"));
    if (!tempVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No temperature sensor data"));
        return;
    }

    double temp = tempVar.toDouble();
    if (qIsNaN(temp)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Temperature sensor unavailable"));
        return;
    }

    setCurrentValue(temp);

    if (temp >= m_minTempC && temp <= m_maxTempC) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Ambient temperature: %1°C — suitable for flight")
                      .arg(temp, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Ambient temperature: %1°C — outside %2°C..%3°C safe range")
                      .arg(temp, 0, 'f', 1).arg(m_minTempC, 0, 'f', 1).arg(m_maxTempC, 0, 'f', 1));
    }
}
