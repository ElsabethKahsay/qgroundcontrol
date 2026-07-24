#include "EkfFailsafeCheck.h"

#include "TelemetryBridge.h"

EkfFailsafeCheck::EkfFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.ekf"),
                    QStringLiteral("EKF Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when FS_EKF_ACTION >= 1 (failsafe action is set).
// Warns if FS_EKF_ACTION == 0 (failsafe disabled); fails for any other non-standard value.
void EkfFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FS_EKF_ACTION")) &&
        !isParamAvailable(QStringLiteral("param_FS_EKF_THRESH"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("FS_EKF params not available from vehicle"));
        return;
    }

    double fsEkfAction = getTelemetryDouble(QStringLiteral("param_FS_EKF_ACTION"));
    double fsEkfThresh = getTelemetryDouble(QStringLiteral("param_FS_EKF_THRESH"));

    if (qFuzzyCompare(fsEkfAction, 0.0)) {
        setStatus(CheckStatus::Warning, QStringLiteral("EKF failsafe disabled"));
        return;
    }

    if (fsEkfAction >= 1.0) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Action: %1, Threshold: %2")
                      .arg(static_cast<int>(fsEkfAction))
                      .arg(fsEkfThresh, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Failed, QStringLiteral("EKF failsafe misconfigured"));
}
