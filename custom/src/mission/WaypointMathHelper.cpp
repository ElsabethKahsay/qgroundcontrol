#include "WaypointMathHelper.h"

#include "WaypointMath.h"

WaypointMathHelper::WaypointMathHelper(QObject *parent)
    : QObject(parent) {}

QGeoCoordinate WaypointMathHelper::coordinateFromBearingAndDistance(
    const QGeoCoordinate &reference,
    double bearingDegrees,
    double distanceMeters) const
{
    return WaypointMath::coordinateFromBearingAndDistance(reference, bearingDegrees, distanceMeters);
}

double WaypointMathHelper::bearingBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to) const
{
    return WaypointMath::bearingBetweenCoordinates(from, to);
}

double WaypointMathHelper::distanceBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to) const
{
    return WaypointMath::distanceBetweenCoordinates(from, to);
}
