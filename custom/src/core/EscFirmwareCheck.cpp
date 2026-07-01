#include "EscFirmwareCheck.h"

#include "TelemetryBridge.h"

EscFirmwareCheck::EscFirmwareCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.esc.firmware"),
                    QStringLiteral("ESC Firmware Version"),
                    CheckCategory::Propulsion, CheckType::Auto, false, true, parent)
{
    m_telemetry = telemetry;
}

void EscFirmwareCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int count = static_cast<int>(getTelemetryDouble(QStringLiteral("escInfoCount")));
    QVariant failureVar = getTelemetryVariant(QStringLiteral("escInfoFailureFlags"));
    QVariant errorVar = getTelemetryVariant(QStringLiteral("escInfoErrorCount"));

    if (count == 0) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("ESC firmware info not available (no ESC_INFO message)"));
        return;
    }

    // Check failure flags
    int totalFailures = 0;
    int totalErrors = 0;
    QStringList escDetails;

    if (failureVar.isValid() && errorVar.isValid()) {
        QVariantList flags = failureVar.toList();
        QVariantList errors = errorVar.toList();
        for (int i = 0; i < qMin(flags.size(), count); ++i) {
            uint16_t f = flags[i].toUInt();
            uint32_t e = errors[i].toUInt();
            if (f != 0)
                totalFailures++;
            totalErrors += static_cast<int>(e);
            escDetails << QStringLiteral("ESC%1: fail=0x%2 err=%3")
                              .arg(i + 1).arg(f, 0, 16).arg(e);
        }
    }

    if (totalFailures > 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("%1/%2 ESC(s) report failure flags — investigate")
                      .arg(totalFailures).arg(count));
        return;
    }

    if (totalErrors > 0) {
        setCurrentValue(QStringLiteral("%1 ESC(s), %2 total errors").arg(count).arg(totalErrors));
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 ESC(s), %2 error(s) — no hard failures")
                      .arg(count).arg(totalErrors));
        return;
    }

    setCurrentValue(QStringLiteral("%1 ESC(s) online, no failures").arg(count));
    setStatus(CheckStatus::Passed,
              QStringLiteral("%1 ESC(s) — no failure flags or errors reported")
                  .arg(count));
}
