#include "GeofenceMaxRadiusCheck.h"

#include "TelemetryBridge.h"

GeofenceMaxRadiusCheck::GeofenceMaxRadiusCheck(TelemetryBridge *telemetry,
                                               double minRadius,
                                               QObject *parent)
    : AbstractCheck(QStringLiteral("safety.geofence.max_radius"),
                    QStringLiteral("Geofence Max Radius"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
    , m_minRadius(minRadius)
{
    m_telemetry = telemetry;
}

void GeofenceMaxRadiusCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_GF_MAX_HORIZ_DIST"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("GF_MAX_HORIZ_DIST not available from vehicle"));
        return;
    }

    double maxDist = getTelemetryDouble(QStringLiteral("param_GF_MAX_HORIZ_DIST"));

    if (maxDist >= m_minRadius) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1m radius").arg(maxDist, 0, 'f', 0));
        return;
    }

    if (maxDist >= 1.0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GF_MAX_HORIZ_DIST=%1m — very small radius")
                      .arg(maxDist, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("GF_MAX_HORIZ_DIST=%.0fm — no radius limit set")
                  .arg(maxDist, 0, 'f', 0));
}
