#include "GeofenceParamCheck.h"

#include "TelemetryBridge.h"

GeofenceParamCheck::GeofenceParamCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.geofence"),
                    QStringLiteral("Geofence Params"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when geofence is disabled, or enabled with both alt+lateral types and an action configured.
// Warns when enabled but missing fence types or no action is set; skips if params are unavailable.
void GeofenceParamCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_GEOFENCE_ENABLED")) &&
        !isParamAvailable(QStringLiteral("param_FENCE_TYPE")) &&
        !isParamAvailable(QStringLiteral("param_GF_ACTION")) &&
        !isParamAvailable(QStringLiteral("param_GF_MAX_HORIZ_DIST")) &&
        !isParamAvailable(QStringLiteral("param_GF_MAX_ALT"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("Geofence params not available from vehicle"));
        return;
    }

    double gfEnabled = getTelemetryDouble(QStringLiteral("param_GEOFENCE_ENABLED"));
    double fenceType = getTelemetryDouble(QStringLiteral("param_FENCE_TYPE"));
    double gfAction = getTelemetryDouble(QStringLiteral("param_GF_ACTION"));
    double maxDist = getTelemetryDouble(QStringLiteral("param_GF_MAX_HORIZ_DIST"));
    double maxAlt = getTelemetryDouble(QStringLiteral("param_GF_MAX_ALT"));

    // If enabled is 0, geofence is genuinely disabled
    if (qFuzzyCompare(gfEnabled, 0.0)) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Disabled"));
        return;
    }

    QString msg;
    bool ok = true;

    if (qFuzzyCompare(gfEnabled, 1.0)) {
        msg = QStringLiteral("Enabled");

        // Verify FENCE_TYPE has both altitude (bit0) and lateral (bit1) set
        int ft = static_cast<int>(fenceType);
        if (ft > 0) {
            if ((ft & 1) && (ft & 2)) {
                msg += QStringLiteral(" — type=alt+lat");
            } else {
                QStringList missing;
                if (!(ft & 1)) missing << QStringLiteral("altitude");
                if (!(ft & 2)) missing << QStringLiteral("lateral");
                msg += QStringLiteral(" — type missing ") + missing.join(QStringLiteral(", "));
                ok = false;
            }
        }

        if (gfAction >= 1.0) {
            msg += QStringLiteral(" — action=%1").arg(static_cast<int>(gfAction));
        } else {
            msg += QStringLiteral(" — no action set");
            ok = false;
        }
        if (maxDist > 0.0)
            msg += QStringLiteral(" — max %1m").arg(maxDist, 0, 'f', 0);
        if (maxAlt > 0.0)
            msg += QStringLiteral(" — max %1m alt").arg(maxAlt, 0, 'f', 0);
    } else {
        msg = QStringLiteral("Disabled");
        ok = true;
    }

    setStatus(ok ? CheckStatus::Passed : CheckStatus::Warning, msg);
}
