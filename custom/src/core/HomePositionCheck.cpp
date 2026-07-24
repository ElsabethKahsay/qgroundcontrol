#include "HomePositionCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

HomePositionCheck::HomePositionCheck(TelemetryBridge *telemetry,
                                     double maxDistKm,
                                     QObject *parent)
    : AbstractCheck(QStringLiteral("nav.home"),
                    QStringLiteral("Home Position"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxDistKm(maxDistKm)
{
    m_telemetry = telemetry;
}

void HomePositionCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double homeLat = getTelemetryDouble(QStringLiteral("homeLatitude"));
    double homeLon = getTelemetryDouble(QStringLiteral("homeLongitude"));
    double homeAlt = getTelemetryDouble(QStringLiteral("homeAltitude"));
    double curLat = getTelemetryDouble(QStringLiteral("gpsLatitude"));
    double curLon = getTelemetryDouble(QStringLiteral("gpsLongitude"));

    setCurrentValue(homeLat);

    if (qFuzzyIsNull(homeLat) && qFuzzyIsNull(homeLon)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Home not set yet"));
        return;
    }

    // Check home is within ~5m of current position (0.005 km threshold)
    // Use latitude-adjusted longitude scaling for better accuracy
    double avgLat = (homeLat + curLat) / 2.0 * M_PI / 180.0;
    double lonScale = qCos(avgLat);
    // Prevent degenerate case at poles
    if (lonScale < 0.01) lonScale = 0.01;
    double dlat = (homeLat - curLat) * 111.0;
    double dlon = (homeLon - curLon) * 111.0 * lonScale;
    double approxDistKm = qSqrt(dlat * dlat + dlon * dlon);

    // Pass if home is within maxDistKm; warn if too far; pending if home not set.
    if (approxDistKm > m_maxDistKm) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Home is %1km away — verify").arg(approxDistKm, 0, 'f', 3));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Home: %1, %2 @ %3m")
                  .arg(homeLat, 0, 'f', 6)
                  .arg(homeLon, 0, 'f', 6)
                  .arg(homeAlt, 0, 'f', 1));
}
