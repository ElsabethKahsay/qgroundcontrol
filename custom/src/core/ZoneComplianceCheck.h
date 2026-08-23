#pragma once

#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QtPositioning/QGeoCoordinate>

#include "AbstractCheck.h"

class MissionController;
class NoFlyZoneModel;

/**
 * @brief Automatic preflight check: verifies the planned mission route does
 *        not intersect any active no-fly zone.
 *
 * Waypoints are extracted from the Plan View's MissionController
 * (visualItems → VisualMissionItem::coordinate()) and tested against every
 * active zone from NoFlyZoneModel using great-circle distance (Haversine via
 * QGeoCoordinate::distanceTo).
 *
 * Status mapping:
 *  - Warning : no mission loaded (nothing to test)
 *  - Passed  : no active zones, or route clear of all of them
 *  - Failed  : route intersects ≥1 zone that has not been acknowledged
 *  - Warning : all intersecting zones acknowledged by the operator
 *
 * Every evaluation whose result signature changes (or that fires after the
 * 60 s re-log interval) writes one zone_compliance_log row per intersecting
 * zone — with zone_id and intersection set — before the status is reported,
 * so the audit trail always matches what the operator saw.
 *
 * Acknowledgement: acknowledgeZone(zoneId, reason) stamps override_reason on
 * the newest compliance row for that zone/flight pair and treats the zone as
 * cleared for the rest of the session.  Previously acknowledged zones are
 * restored from the DB when the flight id changes.
 */
class ZoneComplianceCheck : public AbstractCheck
{
    Q_OBJECT

public:
    explicit ZoneComplianceCheck(NoFlyZoneModel *zoneModel, QObject *parent = nullptr);

    /// Wire to the airspace knowledge base.  Called by PreflightPlugin after
    /// the model is created — check registration happens earlier, during
    /// PreflightManager construction.
    void setZoneModel(NoFlyZoneModel *model);

    /// Wire to the Plan View's MissionController (called by PreflightPlugin
    /// once the controller has been resolved).  Re-evaluates on connect.
    void setMissionController(MissionController *controller);

    Q_INVOKABLE void evaluate() final;

    /// Operator acknowledges the route crossing this zone for the current
    /// flight.  Persists the reason into zone_compliance_log.override_reason.
    Q_INVOKABLE bool acknowledgeZone(int zoneId, const QString &reason);

    /// Zone ids currently intersecting the route but NOT acknowledged.
    Q_INVOKABLE QVariantList unacknowledgedZoneIds() const;
    /// All zone ids currently intersecting the route (acked or not).
    Q_INVOKABLE QVariantList intersectingZoneIds() const { return _intersectingIds(); }

private:
    QList<QGeoCoordinate> _collectWaypoints() const;
    QVariantList _intersectingIds() const;
    void _loadAcknowledgedZones(int flightId);
    void _logAuditTrail(CheckStatus status,
                        const QList<QVariantMap> &intersectingZones,
                        const QStringList &unackedNames);
    QString _signature(CheckStatus status, const QVariantList &unackedIds) const;

    NoFlyZoneModel *_zoneModel = nullptr;
    MissionController *_missionController = nullptr;

    QSet<int> m_ackedZoneIds;      ///< zones acknowledged for current flight
    int m_loadedFlightId = -1;     ///< flight id the ack cache was loaded for

    QString m_lastSignature;       ///< last logged result signature
    QElapsedTimer m_lastLogTimer;  ///< rate-limits repeat audit rows
};
