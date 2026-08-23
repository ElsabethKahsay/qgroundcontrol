#include "ZoneComplianceCheck.h"

#include "DatabaseManager.h"
#include "FlightSession.h"
#include "MissionManager/MissionController.h"
#include "NoFlyZoneModel.h"
#include "OperatorManager.h"
#include "QmlObjectListModel.h"
#include "MissionManager/VisualMissionItem.h"

namespace {
constexpr int kRelogIntervalMs = 60 * 1000;  ///< min interval between repeat audit rows
}

ZoneComplianceCheck::ZoneComplianceCheck(NoFlyZoneModel *zoneModel, QObject *parent)
    : AbstractCheck(QStringLiteral("airspace.zone_compliance"),
                    QStringLiteral("No-Fly Zone Compliance"),
                    CheckCategory::Safety,
                    CheckType::Auto,
                    false,   // mandatory — advisory, does not block arming
                    false,   // canOverride — acks go through acknowledgeZone()
                    parent)
    , _zoneModel(zoneModel)
{
    m_lastLogTimer.start();
}

void ZoneComplianceCheck::setZoneModel(NoFlyZoneModel *model)
{
    if (_zoneModel == model)
        return;
    _zoneModel = model;
    evaluate();
}

void ZoneComplianceCheck::setMissionController(MissionController *controller)
{
    if (_missionController == controller)
        return;

    if (_missionController) {
        disconnect(_missionController, &MissionController::visualItemsChanged,
                   this, &ZoneComplianceCheck::evaluate);
    }
    _missionController = controller;
    if (_missionController) {
        connect(_missionController, &MissionController::visualItemsChanged,
                this, &ZoneComplianceCheck::evaluate);
    }
    evaluate();
}

QList<QGeoCoordinate> ZoneComplianceCheck::_collectWaypoints() const
{
    QList<QGeoCoordinate> waypoints;
    if (!_missionController || !_missionController->visualItems())
        return waypoints;

    QmlObjectListModel *visualItems = _missionController->visualItems();
    for (int i = 0; i < visualItems->count(); ++i) {
        auto *item = qobject_cast<VisualMissionItem *>(visualItems->get(i));
        if (!item)
            continue;
        const QGeoCoordinate coord = item->coordinate();
        if (coord.isValid())
            waypoints.append(coord);
    }
    return waypoints;
}

QVariantList ZoneComplianceCheck::_intersectingIds() const
{
    QVariantList ids;
    if (!_zoneModel)
        return ids;

    const QList<QGeoCoordinate> waypoints = _collectWaypoints();
    const auto zones = _zoneModel->activeZones();
    for (const QVariantMap &zone : zones) {
        const QGeoCoordinate center(zone.value(QStringLiteral("latitude")).toDouble(),
                                    zone.value(QStringLiteral("longitude")).toDouble());
        const double radiusM = zone.value(QStringLiteral("radius_m")).toDouble();
        for (const QGeoCoordinate &wp : waypoints) {
            if (center.distanceTo(wp) < radiusM) {
                ids.append(zone.value(QStringLiteral("id")).toInt());
                break;
            }
        }
    }
    return ids;
}

void ZoneComplianceCheck::_loadAcknowledgedZones(int flightId)
{
    m_ackedZoneIds.clear();
    m_loadedFlightId = flightId;
    if (flightId <= 0)
        return;

    const auto records = DatabaseManager::instance().getComplianceForFlight(flightId);
    for (const QVariantMap &r : records) {
        const int zoneId = r.value(QStringLiteral("zone_id")).toInt();
        const bool intersecting = r.value(QStringLiteral("intersection")).toBool();
        const QString overrideReason = r.value(QStringLiteral("override_reason")).toString();
        if (intersecting && zoneId > 0 && !overrideReason.isEmpty())
            m_ackedZoneIds.insert(zoneId);
    }
}

void ZoneComplianceCheck::evaluate()
{
    if (!_zoneModel) {
        setStatus(CheckStatus::Error, QStringLiteral("Zone model unavailable"));
        return;
    }

    const int flightId = FlightSession::instance()->currentFlightId();
    if (flightId != m_loadedFlightId)
        _loadAcknowledgedZones(flightId);

    const QmlObjectListModel *visualItems =
        _missionController ? _missionController->visualItems() : nullptr;
    if (!visualItems || visualItems->count() <= 1) {
        setStatus(CheckStatus::Warning, QStringLiteral("No mission loaded"));
        return;
    }

    const QList<QGeoCoordinate> waypoints = _collectWaypoints();
    const auto activeZones = _zoneModel->activeZones();
    if (activeZones.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("No active zones defined (%1 waypoints checked)")
                      .arg(waypoints.count()));
        return;
    }

    // Test every waypoint against every active zone.
    QList<QVariantMap> intersectingZones;
    QStringList unackedNames;
    QStringList ackedNames;
    for (const QVariantMap &zone : activeZones) {
        const int zoneId = zone.value(QStringLiteral("id")).toInt();
        const QGeoCoordinate center(zone.value(QStringLiteral("latitude")).toDouble(),
                                    zone.value(QStringLiteral("longitude")).toDouble());
        const double radiusM = zone.value(QStringLiteral("radius_m")).toDouble();

        bool intersects = false;
        for (const QGeoCoordinate &wp : waypoints) {
            if (center.distanceTo(wp) < radiusM) {
                intersects = true;
                break;
            }
        }
        if (!intersects)
            continue;

        intersectingZones.append(zone);
        const QString name = zone.value(QStringLiteral("name")).toString();
        if (m_ackedZoneIds.contains(zoneId))
            ackedNames.append(name);
        else
            unackedNames.append(name);
    }

    if (!unackedNames.isEmpty()) {
        _logAuditTrail(CheckStatus::Failed, intersectingZones, unackedNames);
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Route intersects %1 restricted zone(s): %2")
                      .arg(unackedNames.count())
                      .arg(unackedNames.join(QStringLiteral(", "))));
        return;
    }

    if (!ackedNames.isEmpty()) {
        _logAuditTrail(CheckStatus::Warning, intersectingZones, {});
        setStatus(CheckStatus::Warning,
                  QStringLiteral("All intersecting zones acknowledged (%1)")
                      .arg(ackedNames.join(QStringLiteral(", "))));
        return;
    }

    _logAuditTrail(CheckStatus::Passed, {}, {});
    setStatus(CheckStatus::Passed,
              QStringLiteral("Route clear of all %1 active zone(s)").arg(activeZones.count()));
}

bool ZoneComplianceCheck::acknowledgeZone(int zoneId, const QString &reason)
{
    if (zoneId <= 0)
        return false;

    const int flightId = FlightSession::instance()->currentFlightId();
    if (flightId > 0) {
        DatabaseManager::instance().updateComplianceOverrideReason(zoneId, flightId, reason);
    }
    m_ackedZoneIds.insert(zoneId);
    evaluate();
    return true;
}

QVariantList ZoneComplianceCheck::unacknowledgedZoneIds() const
{
    QVariantList out;
    const QVariantList all = _intersectingIds();
    for (const QVariant &v : all) {
        if (!m_ackedZoneIds.contains(v.toInt()))
            out.append(v);
    }
    return out;
}

void ZoneComplianceCheck::_logAuditTrail(CheckStatus status,
                                         const QList<QVariantMap> &intersectingZones,
                                         const QStringList &unackedNames)
{
    const QString signature = _signature(status, _intersectingIds());
    const bool signatureChanged = signature != m_lastSignature;
    const bool timerExpired = m_lastLogTimer.elapsed() >= kRelogIntervalMs;
    if (!signatureChanged && !timerExpired)
        return;

    m_lastSignature = signature;
    m_lastLogTimer.restart();

    const int flightId = FlightSession::instance()->currentFlightId();
    const int operatorId = OperatorManager::instance()->currentOperatorId();
    auto &db = DatabaseManager::instance();

    const QString result = (status == CheckStatus::Failed)  ? QStringLiteral("fail")
                           : (status == CheckStatus::Warning) ? QStringLiteral("warning")
                                                              : QStringLiteral("pass");

    if (intersectingZones.isEmpty()) {
        QString notes = QStringLiteral("auto-check: route clear");
        if (status == CheckStatus::Warning)
            notes = QStringLiteral("auto-check: no mission loaded");
        db.insertComplianceRecord(flightId, operatorId, result, notes, QString(), -1, false);
        return;
    }

    for (const QVariantMap &zone : intersectingZones) {
        const int zoneId = zone.value(QStringLiteral("id")).toInt();
        const QString name = zone.value(QStringLiteral("name")).toString();
        const bool acked = !unackedNames.contains(name);
        QString notes = QStringLiteral("auto-check: waypoint inside \"%1\"").arg(name);
        if (acked)
            notes += QStringLiteral(" [acknowledged]");
        else if (status == CheckStatus::Failed)
            notes += QStringLiteral(" [UNACKNOWLEDGED]");
        // Persist BEFORE the status is reported so the audit trail always
        // matches what the operator was shown.
        db.insertComplianceRecord(flightId, operatorId, result, notes, QString(), zoneId, true);
    }
}

QString ZoneComplianceCheck::_signature(CheckStatus status, const QVariantList &unackedIds) const
{
    QStringList idStrings;
    for (const QVariant &v : unackedIds)
        idStrings << QString::number(v.toInt());
    idStrings.sort();
    return QStringLiteral("%1:%2").arg(static_cast<int>(status)).arg(idStrings.join(u','));
}
