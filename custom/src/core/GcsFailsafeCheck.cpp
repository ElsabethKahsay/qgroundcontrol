#include "GcsFailsafeCheck.h"

#include "TelemetryBridge.h"

GcsFailsafeCheck::GcsFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.gcs"),
                    QStringLiteral("GCS Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when FS_GCS_ENABLE >= 1 (GCS failsafe action is configured).
// Warns when FS_GCS_ENABLE == 0, meaning the vehicle takes no action if the GCS link is lost.
void GcsFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FS_GCS_ENABLE"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("FS_GCS_ENABLE not available from vehicle"));
        return;
    }

    double fsGcsEnable = getTelemetryDouble(QStringLiteral("param_FS_GCS_ENABLE"));

    if (fsGcsEnable >= 1.0) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Enabled (action=%1)").arg(static_cast<int>(fsGcsEnable)));
        return;
    }

    setStatus(CheckStatus::Warning, QStringLiteral("GCS failsafe disabled"));
}
