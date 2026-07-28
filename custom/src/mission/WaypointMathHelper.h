#pragma once
#include <QObject>
#include <QGeoCoordinate>

/// QML-accessible wrapper around WaypointMath static functions.
/// Exposes bearing, distance, and coordinate-offset calculations to QML/JS
/// via Q_INVOKABLE methods so mission editors can compute geometry client-side.
///
/// All methods are thin delegations to the WaypointMath utility class;
/// this wrapper exists solely to bridge static C++ functions into the QML runtime.
class WaypointMathHelper : public QObject {
    Q_OBJECT
public:
    explicit WaypointMathHelper(QObject *parent = nullptr);

    /// Compute a new coordinate by moving bearingDegrees from reference by distanceMeters.
    Q_INVOKABLE QGeoCoordinate coordinateFromBearingAndDistance(
        const QGeoCoordinate &reference,
        double bearingDegrees,
        double distanceMeters) const;

    /// Return the initial bearing in degrees [0, 360) from `from` toward `to`.
    Q_INVOKABLE double bearingBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to) const;

    /// Return the great-circle distance in meters between two coordinates (Haversine).
    Q_INVOKABLE double distanceBetweenCoordinates(
        const QGeoCoordinate &from,
        const QGeoCoordinate &to) const;
};
