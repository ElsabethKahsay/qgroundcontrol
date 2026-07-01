#include "GeofenceMaxAltCheck.h"

#include "TelemetryBridge.h"

GeofenceMaxAltCheck::GeofenceMaxAltCheck(TelemetryBridge *telemetry,
                                         double minAlt,
                                         QObject *parent)
    : AbstractCheck(QStringLiteral("safety.geofence.max_alt"),
                    QStringLiteral("Geofence Max Altitude"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
    , m_minAlt(minAlt)
{
    m_telemetry = telemetry;
}

void GeofenceMaxAltCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_GF_MAX_ALT"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("GF_MAX_ALT not available from vehicle"));
        return;
    }

    double maxAlt = getTelemetryDouble(QStringLiteral("param_GF_MAX_ALT"));

    if (maxAlt < m_minAlt) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GF_MAX_ALT=%.0fm — very low limit")
                      .arg(maxAlt, 0, 'f', 0));
        return;
    }

    if (maxAlt >= 1.0) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1m altitude ceiling").arg(maxAlt, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("GF_MAX_ALT=%.0fm — no altitude limit set")
                  .arg(maxAlt, 0, 'f', 0));
}
