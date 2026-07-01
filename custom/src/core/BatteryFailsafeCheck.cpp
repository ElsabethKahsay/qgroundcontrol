#include "BatteryFailsafeCheck.h"

#include "TelemetryBridge.h"

BatteryFailsafeCheck::BatteryFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.battery"),
                    QStringLiteral("Battery Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

void BatteryFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_BATT_FS_LOW_ACT")) &&
        !isParamAvailable(QStringLiteral("param_FS_BATT_ENABLE")) &&
        !isParamAvailable(QStringLiteral("param_BAT_LOW_THR"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("Battery failsafe params not available from vehicle"));
        return;
    }

    double battFsLowAct = getTelemetryDouble(QStringLiteral("param_BATT_FS_LOW_ACT"));
    double fsBattEnable = getTelemetryDouble(QStringLiteral("param_FS_BATT_ENABLE"));
    double batLowThr = getTelemetryDouble(QStringLiteral("param_BAT_LOW_THR"));

    QStringList issues;

    // Check threshold is configured in a reasonable range
    if (isParamAvailable(QStringLiteral("param_BAT_LOW_THR")) && (batLowThr < 10.0 || batLowThr > 35.0)) {
        issues << QStringLiteral("BAT_LOW_THR=%1% (expected 10-35%)").arg(static_cast<int>(batLowThr));
    }

    // Check action
    if (!qFuzzyIsNull(battFsLowAct)) {
        if (battFsLowAct < 1.0)
            issues << QStringLiteral("BATT_FS_LOW_ACT=%1 (need ≥1)").arg(static_cast<int>(battFsLowAct));
    } else if (!qFuzzyIsNull(fsBattEnable) && qFuzzyCompare(fsBattEnable, 1.0)) {
        issues << QStringLiteral("FS_BATT_ENABLE=1 but no BATT_FS_LOW_ACT");
    }

    if (issues.isEmpty()) {
        if (!qFuzzyIsNull(battFsLowAct) && battFsLowAct >= 1.0) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("Action %1, threshold %2%")
                          .arg(static_cast<int>(battFsLowAct)).arg(static_cast<int>(batLowThr)));
        } else {
            setStatus(CheckStatus::Warning, QStringLiteral("Battery failsafe disabled"));
        }
    } else {
        setStatus(CheckStatus::Failed, issues.join(QStringLiteral(", ")));
    }
}
