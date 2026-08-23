#include "NoFlyZoneModel.h"

#include "DatabaseManager.h"
#include "FlightSession.h"
#include "OperatorManager.h"

#include <QGeoCoordinate>
#include <QtMath>

#include <algorithm>

NoFlyZoneModel::NoFlyZoneModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

int NoFlyZoneModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_visibleZones.size();
}

QVariant NoFlyZoneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleZones.size())
        return {};

    const QVariantMap &z = m_visibleZones.at(index.row());
    switch (role) {
    case ZoneIdRole:       return z.value(QStringLiteral("id"));
    case NameRole:         return z.value(QStringLiteral("name"));
    case DescriptionRole:  return z.value(QStringLiteral("description"));
    case LatitudeRole:     return z.value(QStringLiteral("latitude"));
    case LongitudeRole:    return z.value(QStringLiteral("longitude"));
    case RadiusMRole:      return z.value(QStringLiteral("radius_m"));
    case ReasonRole:       return z.value(QStringLiteral("reason"));
    case ActiveRole:       return z.value(QStringLiteral("active"));
    case CreatedAtRole:    return z.value(QStringLiteral("created_at"));
    case UpdatedAtRole:    return z.value(QStringLiteral("updated_at"));
    case CreatedByNameRole:return z.value(QStringLiteral("created_by_name"));
    }
    return {};
}

QHash<int, QByteArray> NoFlyZoneModel::roleNames() const
{
    return {
        { ZoneIdRole,        "zoneId" },
        { NameRole,          "name" },
        { DescriptionRole,   "description" },
        { LatitudeRole,      "latitude" },
        { LongitudeRole,     "longitude" },
        { RadiusMRole,       "radiusM" },
        { ReasonRole,        "reason" },
        { ActiveRole,        "active" },
        { CreatedAtRole,     "createdAt" },
        { UpdatedAtRole,     "updatedAt" },
        { CreatedByNameRole, "createdByName" },
    };
}

void NoFlyZoneModel::reload()
{
    beginResetModel();
    m_zones = DatabaseManager::instance().getAllZones();
    _applyFilterSort();
    endResetModel();
    emit countChanged();
    emit zonesChanged();
    _reloadCompliance();
}

// ── Search / sort ────────────────────────────────────────────────────────────

void NoFlyZoneModel::setNameFilter(const QString &filter)
{
    if (_nameFilter == filter)
        return;
    _nameFilter = filter;
    beginResetModel();
    _applyFilterSort();
    endResetModel();
    emit nameFilterChanged();
}

void NoFlyZoneModel::setSortMode(const QString &mode)
{
    if (_sortMode == mode)
        return;
    _sortMode = mode;
    beginResetModel();
    _applyFilterSort();
    endResetModel();
    emit sortModeChanged();
}

void NoFlyZoneModel::_applyFilterSort()
{
    m_visibleZones.clear();

    const QString needle = _nameFilter.trimmed();
    for (const QVariantMap &z : m_zones) {
        if (!needle.isEmpty()
            && !z.value(QStringLiteral("name")).toString().contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        m_visibleZones.append(z);
    }

    const auto nameOf = [](const QVariantMap &z) {
        return z.value(QStringLiteral("name")).toString();
    };

    if (_sortMode == QStringLiteral("radius")) {
        std::sort(m_visibleZones.begin(), m_visibleZones.end(),
                  [](const QVariantMap &a, const QVariantMap &b) {
                      return a.value(QStringLiteral("radius_m")).toDouble()
                           > b.value(QStringLiteral("radius_m")).toDouble();
                  });
    } else if (_sortMode == QStringLiteral("reason")) {
        std::sort(m_visibleZones.begin(), m_visibleZones.end(),
                  [nameOf](const QVariantMap &a, const QVariantMap &b) {
                      return nameOf(a).compare(nameOf(b), Qt::CaseInsensitive) < 0;
                  });
        // Secondary key within same reason class: name A→Z.
        std::stable_sort(m_visibleZones.begin(), m_visibleZones.end(),
                         [](const QVariantMap &a, const QVariantMap &b) {
                             return a.value(QStringLiteral("reason")).toString()
                                  < b.value(QStringLiteral("reason")).toString();
                         });
    } else if (_sortMode == QStringLiteral("updated")) {
        std::sort(m_visibleZones.begin(), m_visibleZones.end(),
                  [](const QVariantMap &a, const QVariantMap &b) {
                      return a.value(QStringLiteral("updated_at")).toString()
                           > b.value(QStringLiteral("updated_at")).toString();
                  });
    } else {  // "name" (default): case-insensitive A→Z
        std::sort(m_visibleZones.begin(), m_visibleZones.end(),
                  [nameOf](const QVariantMap &a, const QVariantMap &b) {
                      return nameOf(a).compare(nameOf(b), Qt::CaseInsensitive) < 0;
                  });
    }
}

// ── Zone CRUD ────────────────────────────────────────────────────────────────

int NoFlyZoneModel::createZone(const QString &name, const QString &description,
                               double lat, double lon, double radiusM,
                               const QString &reason)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Zone name cannot be empty."));
        return -1;
    }

    auto &db = DatabaseManager::instance();
    if (db.getZoneByName(trimmed).value(QStringLiteral("id"), -1).toInt() > 0) {
        emit errorOccurred(tr("A zone named \"%1\" already exists.").arg(trimmed));
        return -1;
    }

    int createdBy = OperatorManager::instance()->currentOperatorId();
    int id = db.insertZone(trimmed, description, lat, lon, radiusM, reason, createdBy);
    if (id > 0) {
        reload();
        _checkOverlapAfterSave(id, lat, lon, radiusM);
    }
    return id;
}

bool NoFlyZoneModel::updateZone(int id, const QString &name, const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, bool active)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Zone name cannot be empty."));
        return false;
    }

    auto &db = DatabaseManager::instance();
    const QVariantMap existing = db.getZoneByName(trimmed);
    const int existingId = existing.value(QStringLiteral("id"), -1).toInt();
    if (existingId > 0 && existingId != id) {
        emit errorOccurred(tr("A zone named \"%1\" already exists.").arg(trimmed));
        return false;
    }

    bool ok = db.updateZone(id, trimmed, description, lat, lon, radiusM, reason, active);
    if (ok) {
        reload();
        _checkOverlapAfterSave(id, lat, lon, radiusM);
    }
    return ok;
}

bool NoFlyZoneModel::removeZone(int id)
{
    bool ok = DatabaseManager::instance().deleteZone(id);
    if (ok)
        reload();
    return ok;
}

bool NoFlyZoneModel::toggleZoneActive(int id)
{
    for (const QVariantMap &z : m_zones) {
        if (z.value(QStringLiteral("id")).toInt() == id) {
            return updateZone(id,
                              z.value(QStringLiteral("name")).toString(),
                              z.value(QStringLiteral("description")).toString(),
                              z.value(QStringLiteral("latitude")).toDouble(),
                              z.value(QStringLiteral("longitude")).toDouble(),
                              z.value(QStringLiteral("radius_m")).toDouble(),
                              z.value(QStringLiteral("reason")).toString(),
                              !z.value(QStringLiteral("active")).toBool());
        }
    }
    return false;
}

int NoFlyZoneModel::activeCount() const
{
    int n = 0;
    for (const QVariantMap &z : m_zones) {
        if (z.value(QStringLiteral("active")).toBool())
            ++n;
    }
    return n;
}

QList<QVariantMap> NoFlyZoneModel::activeZones() const
{
    QList<QVariantMap> out;
    for (const QVariantMap &z : m_zones) {
        if (z.value(QStringLiteral("active")).toBool())
            out.append(z);
    }
    return out;
}

void NoFlyZoneModel::_checkOverlapAfterSave(int id, double lat, double lon, double radiusM)
{
    const QGeoCoordinate center(lat, lon);
    for (const QVariantMap &other : m_zones) {
        const int otherId = other.value(QStringLiteral("id")).toInt();
        if (otherId == id || !other.value(QStringLiteral("active")).toBool())
            continue;
        const QGeoCoordinate otherCenter(other.value(QStringLiteral("latitude")).toDouble(),
                                         other.value(QStringLiteral("longitude")).toDouble());
        const double sumRadii = radiusM
                              + other.value(QStringLiteral("radius_m")).toDouble();
        if (center.distanceTo(otherCenter) < sumRadii) {
            emit overlapWarning(tr("\"%1\" overlaps existing zone \"%2\".")
                                    .arg(nameFromId(id), nameFromId(otherId)));
            return;
        }
    }
}

QString NoFlyZoneModel::nameFromId(int id) const
{
    for (const QVariantMap &z : m_zones) {
        if (z.value(QStringLiteral("id")).toInt() == id)
            return z.value(QStringLiteral("name")).toString();
    }
    return QStringLiteral("#%1").arg(id);
}

// ── Manual compliance ────────────────────────────────────────────────────────

bool NoFlyZoneModel::submitCompliance(const QString &result,
                                      const QString &notes,
                                      const QString &overrideReason)
{
    int flightId = FlightSession::instance()->currentFlightId();
    int operatorId = OperatorManager::instance()->currentOperatorId();

    bool ok = DatabaseManager::instance().insertComplianceRecord(
        flightId, operatorId, result, notes, overrideReason);
    if (ok) {
        _complianceChecked = true;
        emit complianceCheckedChanged();
        _reloadCompliance();
    }
    return ok;
}

void NoFlyZoneModel::resetCompliance()
{
    if (_complianceChecked) {
        _complianceChecked = false;
        emit complianceCheckedChanged();
    }
}

void NoFlyZoneModel::_reloadCompliance()
{
    m_complianceRecords.clear();
    const auto records = DatabaseManager::instance().getComplianceHistory(100);
    for (const auto &record : records) {
        m_complianceRecords.append(record);
    }
    emit complianceRecordsChanged();
}
