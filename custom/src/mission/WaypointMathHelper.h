#pragma once
#include <QObject>
#include <QGeoCoordinate>

class WaypointMathHelper : public QObject {
    Q_OBJECT
public:
    explicit WaypointMathHelper(QObject *parent = nullptr);

    Q_INVOKABLE QGeoCoordinate coordinateFromBearingAndDistance(
        const QGeoCoordinate &reference,
        double bearingDegrees,
        double distanceMeters) const;

    Q_INVOKABLE double bearingBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to) const;

    Q_INVOKABLE double distanceBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to) const;
};
