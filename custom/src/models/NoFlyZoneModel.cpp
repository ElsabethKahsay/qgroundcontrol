#include "NoFlyZoneModel.h"

#include "DatabaseManager.h"
#include "FlightSession.h"
#include "OperatorManager.h"

NoFlyZoneModel::NoFlyZoneModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

int NoFlyZoneModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_zones.size();
}

QVariant NoFlyZoneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_zones.size())
        return {};

    const QVariantMap &z = m_zones.at(index.row());
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
    endResetModel();
    emit countChanged();
    _reloadCompliance();
}

// ── Zone CRUD ────────────────────────────────────────────────────────────────

int NoFlyZoneModel::createZone(const QString &name, const QString &description,
                               double lat, double lon, double radiusM,
                               const QString &reason)
{
    auto &db = DatabaseManager::instance();
    int createdBy = OperatorManager::instance()->currentOperatorId();
    int id = db.insertZone(name, description, lat, lon, radiusM, reason, createdBy);
    if (id > 0)
        reload();
    return id;
}

bool NoFlyZoneModel::updateZone(int id, const QString &name, const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, bool active)
{
    bool ok = DatabaseManager::instance().updateZone(id, name, description, lat, lon, radiusM, reason, active);
    if (ok)
        reload();
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
