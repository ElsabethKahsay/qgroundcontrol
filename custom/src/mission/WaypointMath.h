#pragma once
#include <QGeoCoordinate>

/// Pure-math utility for geodesic calculations on a spherical Earth model.
/// Used by mission planning to compute waypoint offsets, bearings, and distances
/// without pulling in a full GIS library.
class WaypointMath {
public:
    /// Given a start point, bearing, and distance, return the destination coordinate (vincenty-style).
    static QGeoCoordinate coordinateFromBearingAndDistance(
        const QGeoCoordinate &reference,
        double bearingDegrees,
        double distanceMeters);

    /// Initial bearing (forward azimuth) in degrees [0, 360) from `from` to `to`.
    static double bearingBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to);

    /// Great-circle distance in meters between two coordinates (Haversine formula).
    static double distanceBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to);
};
