#include "EscResponsivenessCheck.h"

#include "TelemetryBridge.h"

EscResponsivenessCheck::EscResponsivenessCheck(TelemetryBridge *telemetry,
                                               uint16_t minPwm, uint16_t maxPwm,
                                               QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.esc.responsiveness"),
                    QStringLiteral("ESC Responsiveness"),
                    CheckCategory::Propulsion, CheckType::Auto, false, true, parent)
    , m_minPwm(minPwm)
    , m_maxPwm(maxPwm)
{
    m_telemetry = telemetry;
}

// Pass: all motor PWM outputs within [minPwm, maxPwm] range. Fail: any channel invalid.
void EscResponsivenessCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant outputsVar = getTelemetryVariant(QStringLiteral("motorOutputs"));
    if (!outputsVar.isValid() || outputsVar.toList().isEmpty()) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No SERVO_OUTPUT_RAW telemetry"));
        return;
    }

    QVariantList outputs = outputsVar.toList();
    QStringList invalid;
    for (int i = 0; i < qMin(outputs.size(), 4); ++i) {
        uint16_t pwm = outputs[i].toUInt();
        if (pwm == 0 || pwm > m_maxPwm || pwm < m_minPwm)
            invalid.append(QStringLiteral("Ch%1=%2").arg(i + 1).arg(pwm));
    }

    setCurrentValue(outputs);

    if (invalid.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("ESC outputs valid (Ch1-4)"));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Invalid ESC PWM: %1").arg(invalid.join(QStringLiteral(", "))));
    }
}
