#include "GpsBaroAltConsistencyCheck.h"

#include "TelemetryBridge.h"

GpsBaroAltConsistencyCheck::GpsBaroAltConsistencyCheck(TelemetryBridge *telemetry,
                                                       double maxDeltaM,
                                                       QObject *parent)
    : AbstractCheck(QStringLiteral("nav.altitude.gps_baro_consistency"),
                    QStringLiteral("GPS/Baro Altitude Consistency"),
                    CheckCategory::Navigation, CheckType::Auto, false, true, parent)
    , m_maxDeltaM(maxDeltaM)
{
    m_telemetry = telemetry;
}

void GpsBaroAltConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double gpsAlt = getTelemetryDouble(QStringLiteral("gpsAltitude"));
    double relAlt = getTelemetryDouble(QStringLiteral("altitudeRelative"));

    if (qIsNaN(gpsAlt)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No GPS altitude data"));
        return;
    }

    if (qFuzzyIsNull(relAlt) && qFuzzyIsNull(gpsAlt)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for altitude data"));
        return;
    }

    double delta = qAbs(gpsAlt - relAlt);
    setCurrentValue(delta);

    if (delta <= m_maxDeltaM) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("GPS alt %1 m / rel alt %2 m (Δ%3 m)")
                      .arg(gpsAlt, 0, 'f', 1).arg(relAlt, 0, 'f', 1).arg(delta, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GPS/baro altitude delta %1 m exceeds %2 m limit")
                      .arg(delta, 0, 'f', 1).arg(m_maxDeltaM, 0, 'f', 0));
    }
}
