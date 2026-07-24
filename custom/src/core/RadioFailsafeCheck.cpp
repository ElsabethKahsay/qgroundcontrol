#include "RadioFailsafeCheck.h"

#include "TelemetryBridge.h"

RadioFailsafeCheck::RadioFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.radio"),
                    QStringLiteral("Radio Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

void RadioFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FS_THR_ENABLE"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("FS_THR_ENABLE not available from vehicle"));
        return;
    }

    double fsThrEnable = getTelemetryDouble(QStringLiteral("param_FS_THR_ENABLE"));

    // Pass if FS_THR_ENABLE >= 1 (failsafe active); warn if disabled.
    if (fsThrEnable >= 1.0) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Enabled (action=%1)").arg(static_cast<int>(fsThrEnable)));
        return;
    }

    setStatus(CheckStatus::Warning, QStringLiteral("Radio failsafe disabled"));
}
