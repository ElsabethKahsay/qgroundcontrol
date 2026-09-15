#include "DualGpsConsistencyCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"
#include "utils/Config.h"
#include "utils/GeoUtil.h"

DualGpsConsistencyCheck::DualGpsConsistencyCheck(TelemetryBridge *telemetry,
                                                 double maxDivergenceM,
                                                 QObject *parent)
    : AbstractCheck(QStringLiteral("nav.gps.dual_consistency"),
                    QStringLiteral("Dual-GPS Consistency"),
                    CheckCategory::Navigation, CheckType::Auto, false, true, parent)
    , m_maxDivergenceM(maxDivergenceM)
{
    m_telemetry = telemetry;
}

double DualGpsConsistencyCheck::_haversineM(double lat1, double lon1,
                                             double lat2, double lon2)
{
    // Shared implementation — must stay in sync with GeoUtil (R10).
    return GeoUtil::haversineMeters(lat1, lon1, lat2, lon2);
}

// Compute haversine distance between GPS1 and GPS2 positions; fail if divergence > max.
void DualGpsConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int gps1Fix = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsFixType")));
    int gps2Fix = static_cast<int>(getTelemetryDouble(QStringLiteral("gps2FixType")));
    double gps2Lat = getTelemetryDouble(QStringLiteral("gps2Latitude"));
    double gps2Lon = getTelemetryDouble(QStringLiteral("gps2Longitude"));

    if (gps2Fix < 3 || (qFuzzyIsNull(gps2Lat) && qFuzzyIsNull(gps2Lon))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No secondary GPS — dual-GPS not fitted"));
        return;
    }

    if (gps1Fix < 3) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Primary GPS has no fix"));
        return;
    }

    double gps1Lat = getTelemetryDouble(QStringLiteral("gpsLatitude"));
    double gps1Lon = getTelemetryDouble(QStringLiteral("gpsLongitude"));

    if (qFuzzyIsNull(gps1Lat) && qFuzzyIsNull(gps1Lon)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Primary GPS position not yet valid"));
        return;
    }

    double dist = _haversineM(gps1Lat, gps1Lon, gps2Lat, gps2Lon);

    if (dist <= m_maxDivergenceM) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("GPS1/GPS2 divergence: %1 m — within %2 m limit")
                      .arg(dist, 0, 'f', 1).arg(m_maxDivergenceM, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("GPS1/GPS2 divergence: %1 m — exceeds %2 m limit")
                      .arg(dist, 0, 'f', 1).arg(m_maxDivergenceM, 0, 'f', 1));
    }
}

