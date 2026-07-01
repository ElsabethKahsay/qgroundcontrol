#pragma once
#include <QGeoCoordinate>

class WaypointMath {
public:
    static QGeoCoordinate coordinateFromBearingAndDistance(
        const QGeoCoordinate &reference,
        double bearingDegrees,
        double distanceMeters);

    static double bearingBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to);

    static double distanceBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to);
};
