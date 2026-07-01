#include "RtlTerrainCheck.h"

#include "TelemetryBridge.h"

RtlTerrainCheck::RtlTerrainCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.rtl_terrain"),
                    QStringLiteral("RTL Terrain Mode"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void RtlTerrainCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_RTL_ALT_TYPE")) &&
        !isParamAvailable(QStringLiteral("param_RTL_CONE_SLOPE"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("RTL params not available from vehicle"));
        return;
    }

    double rtlAltType = getTelemetryDouble(QStringLiteral("param_RTL_ALT_TYPE"));
    int val = static_cast<int>(rtlAltType);

    QStringList issues;
    QStringList info;

    if (val == 1) {
        info << QStringLiteral("terrain follow");
    } else if (val == 0) {
        issues << QStringLiteral("relative altitude — no terrain awareness");
    } else {
        issues << QStringLiteral("RTL_ALT_TYPE=%1 unexpected").arg(val);
    }

    // Check RTL_CONE_SLOPE (PX4)
    if (isParamAvailable(QStringLiteral("param_RTL_CONE_SLOPE"))) {
        double slope = getTelemetryDouble(QStringLiteral("param_RTL_CONE_SLOPE"));
        int slopeVal = static_cast<int>(slope);
        if (slopeVal == 0) {
            issues << QStringLiteral("RTL_CONE_SLOPE=0 no cone — terrain collision risk");
        } else if (slopeVal >= 1 && slopeVal <= 3) {
            info << QStringLiteral("cone slope %1").arg(slopeVal);
        } else {
            issues << QStringLiteral("RTL_CONE_SLOPE=%1 unexpected").arg(slopeVal);
        }
    }

    if (!issues.isEmpty()) {
        setStatus(CheckStatus::Warning, issues.join(QStringLiteral("; ")));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("RTL_ALT_TYPE=%1 — %2").arg(val).arg(info.join(QStringLiteral(", "))));
}
