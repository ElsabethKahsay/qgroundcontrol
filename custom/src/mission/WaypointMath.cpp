#include "WaypointMath.h"

#include <QtMath>

#include <cmath>

static constexpr double EARTH_RADIUS_M = 6371000.0;

static double toRad(double deg) { return deg * M_PI / 180.0; }
static double toDeg(double rad) { return rad * 180.0 / M_PI; }

QGeoCoordinate WaypointMath::coordinateFromBearingAndDistance(
    const QGeoCoordinate &reference,
    double bearingDegrees,
    double distanceMeters)
{
    double lat1 = toRad(reference.latitude());
    double lon1 = toRad(reference.longitude());
    double brng = toRad(bearingDegrees);
    double d = distanceMeters;

    double lat2 = std::asin(
        std::sin(lat1) * std::cos(d / EARTH_RADIUS_M) +
        std::cos(lat1) * std::sin(d / EARTH_RADIUS_M) * std::cos(brng));

    double lon2 = lon1 + std::atan2(
        std::sin(brng) * std::sin(d / EARTH_RADIUS_M) * std::cos(lat1),
        std::cos(d / EARTH_RADIUS_M) - std::sin(lat1) * std::sin(lat2));

    return QGeoCoordinate(toDeg(lat2), toDeg(lon2));
}

double WaypointMath::bearingBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to)
{
    double lat1 = toRad(from.latitude());
    double lat2 = toRad(to.latitude());
    double lon1 = toRad(from.longitude());
    double lon2 = toRad(to.longitude());
    double dLon = lon2 - lon1;

    double y = std::sin(dLon) * std::cos(lat2);
    double x = std::cos(lat1) * std::sin(lat2) -
               std::sin(lat1) * std::cos(lat2) * std::cos(dLon);

    double brng = std::atan2(y, x);
    return std::fmod(toDeg(brng) + 360.0, 360.0);
}

double WaypointMath::distanceBetweenCoordinates(
    const QGeoCoordinate &from,
    const QGeoCoordinate &to)
{
    double lat1 = toRad(from.latitude());
    double lat2 = toRad(to.latitude());
    double dLat = lat2 - lat1;
    double dLon = toRad(to.longitude()) - toRad(from.longitude());

    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return EARTH_RADIUS_M * c;
}
