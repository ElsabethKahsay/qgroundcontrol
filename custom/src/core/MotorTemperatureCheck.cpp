#include "MotorTemperatureCheck.h"

#include "TelemetryBridge.h"

MotorTemperatureCheck::MotorTemperatureCheck(TelemetryBridge *telemetry,
                                             double maxTempC, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.motor.temperature"),
                    QStringLiteral("Motor Temperature"),
                    CheckCategory::Propulsion, CheckType::Auto, false, true, parent)
    , m_maxTempC(maxTempC)
{
    m_telemetry = telemetry;
}

// Pass: all motor temps <= maxTempC (default 80 C). Fail: any motor overheating.
void MotorTemperatureCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant tempsVar = getTelemetryVariant(QStringLiteral("escTemperatures"));
    if (!tempsVar.isValid() || tempsVar.toList().isEmpty()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No ESC telemetry"));
        return;
    }

    QVariantList temps = tempsVar.toList();
    setCurrentValue(temps);

    QStringList hotMotors;
    int maxBad = qMin(temps.size(), 4);
    for (int i = 0; i < maxBad; ++i) {
        double t = temps[i].toDouble();
        if (t > m_maxTempC)
            hotMotors.append(QStringLiteral("M%1=%2°C").arg(i + 1).arg(t, 0, 'f', 0));
    }

    if (hotMotors.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Temps OK: %1°C max")
                      .arg(qMax(0.0, maxBad > 0 ? temps[0].toDouble() : 0.0), 0, 'f', 0));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Overheating: %1").arg(hotMotors.join(QStringLiteral(", "))));
    }
}
