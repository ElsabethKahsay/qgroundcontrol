#include "ManualConfirmCheck.h"

#include "TelemetryBridge.h"

ManualConfirmCheck::ManualConfirmCheck(const QString &id, const QString &label,
                                       CheckCategory category,
                                       const QString &prompt,
                                       const QStringList &paramNames,
                                       QObject *parent)
    : AbstractCheck(id, label, category, CheckType::Manual, false, true, parent)
    , m_prompt(prompt)
    , m_paramNames(paramNames)
{
    m_telemetry = nullptr;
}

// Pass: already confirmed by user (no-op). Otherwise sets Pending with prompt text.
// Appends available parameter values to the prompt when paramNames are specified.
void ManualConfirmCheck::evaluate()
{
    if (status() == CheckStatus::Passed) return;
    if (m_paramNames.isEmpty()) {
        setStatus(CheckStatus::Pending, m_prompt);
        return;
    }

    // Build message with param values if telemetry is available
    QStringList parts;
    parts << m_prompt;

    for (const QString &pn : m_paramNames) {
        if (isParamAvailable(QStringLiteral("param_") + pn)) {
            double val = getTelemetryDouble(QStringLiteral("param_") + pn);
            parts << QStringLiteral("%1: %2").arg(pn).arg(val, 0, 'f', 1);
        } else if (isParamAvailable(pn)) {
            double val = getTelemetryDouble(pn);
            parts << QStringLiteral("%1: %2").arg(pn).arg(val, 0, 'f', 1);
        }
    }

    setStatus(CheckStatus::Pending, parts.join(QStringLiteral(" | ")));
}
