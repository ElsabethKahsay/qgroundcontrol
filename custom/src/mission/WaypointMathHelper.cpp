// WaypointMathHelper.cpp -- QML wrapper delegating to WaypointMath static methods.

#include "WaypointMathHelper.h"

#include "WaypointMath.h"

WaypointMathHelper::WaypointMathHelper(QObject *parent)
    : QObject(parent) {}

/// Delegate to WaypointMath: spherical-law-of-cosines destination calculation.
QGeoCoordinate WaypointMathHelper::coordinateFromBearingAndDistance(
    const QGeoCoordinate &reference,
    double bearingDegrees,
    double distanceMeters) const
{
    return WaypointMath::coordinateFromBearingAndDistance(reference, bearingDegrees, distanceMeters);
}

/// Delegate to WaypointMath: forward azimuth between two coordinates.
double WaypointMathHelper::bearingBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to) const
{
    return WaypointMath::bearingBetweenCoordinates(from, to);
}

/// Delegate to WaypointMath: Haversine great-circle distance.
double WaypointMathHelper::distanceBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to) const
{
    return WaypointMath::distanceBetweenCoordinates(from, to);
}
