#pragma once

// ============================================================================
// GeoUtil — Shared geodesic helpers.
//
// haversineMeters computes the great-circle distance between two lat/lon
// points. Kept here as a single source of truth so the telemetry-event logger
// and the dual-GPS consistency check cannot drift (R10).
// ============================================================================

#include <QtMath>

#include "Config.h"

namespace GeoUtil {

// Haversine great-circle distance between two lat/lon points (degrees → meters).
inline double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2.0) * qSin(dLat / 2.0)
                   + qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2))
                     * qSin(dLon / 2.0) * qSin(dLon / 2.0);
    return kEarthRadiusM * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
}

} // namespace GeoUtil