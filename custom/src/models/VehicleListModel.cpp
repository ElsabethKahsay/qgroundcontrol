#include "VehicleListModel.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include "DatabaseManager.h"

Q_LOGGING_CATEGORY(vehicleListDebug, "vehicle.list")

VehicleListModel::VehicleListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VehicleListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_vehicles.size();
}

QVariant VehicleListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_vehicles.size())
        return {};

    const QVariantMap &v = m_vehicles.at(index.row());
    switch (role) {
    case DeviceUidRole:         return v.value(QStringLiteral("deviceUid"));
    case FriendlyNameRole:      return v.value(QStringLiteral("friendlyName"));
    case AutopilotTypeRole:     return v.value(QStringLiteral("autopilotType"));
    case AirframeTypeRole:      return v.value(QStringLiteral("airframeType"));
    case VehicleTypeNameRole:   return v.value(QStringLiteral("vehicleTypeName"));
    case FirmwareVersionRole:   return v.value(QStringLiteral("firmwareVersion"));
    case BoardVersionRole:      return v.value(QStringLiteral("boardVersion"));
    case HardwareUidRole:       return v.value(QStringLiteral("hardwareUid"));
    case FingerprintRole:       return v.value(QStringLiteral("fingerprint"));
    case FingerprintSourceRole: return v.value(QStringLiteral("fingerprintSource"));
    case SysidRole:             return v.value(QStringLiteral("sysid"));
    case CompidRole:            return v.value(QStringLiteral("compid"));
    case FrameClassRole:        return v.value(QStringLiteral("frameClass"));
    case MotorCountRole:        return v.value(QStringLiteral("motorCount"));
    case FirstSeenRole:         return v.value(QStringLiteral("firstSeen"));
    case LastSeenRole:          return v.value(QStringLiteral("lastSeen"));
    case TotalFlightCountRole:  return v.value(QStringLiteral("totalFlightCount"));
    case TotalFlightTimeSecRole: return v.value(QStringLiteral("totalFlightTimeSec"));
    case TotalFlightHoursRole:  return v.value(QStringLiteral("totalFlightHours"));
    case NotesRole:             return v.value(QStringLiteral("notes"));
    default:
        return {};
    }
}

QHash<int, QByteArray> VehicleListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names[DeviceUidRole]          = "deviceUid";
    names[FriendlyNameRole]       = "friendlyName";
    names[AutopilotTypeRole]      = "autopilotType";
    names[AirframeTypeRole]       = "airframeType";
    names[VehicleTypeNameRole]    = "vehicleTypeName";
    names[FirmwareVersionRole]    = "firmwareVersion";
    names[BoardVersionRole]       = "boardVersion";
    names[HardwareUidRole]        = "hardwareUid";
    names[FingerprintRole]        = "fingerprint";
    names[FingerprintSourceRole]  = "fingerprintSource";
    names[SysidRole]              = "sysid";
    names[CompidRole]             = "compid";
    names[FrameClassRole]         = "frameClass";
    names[MotorCountRole]         = "motorCount";
    names[FirstSeenRole]          = "firstSeen";
    names[LastSeenRole]           = "lastSeen";
    names[TotalFlightCountRole]   = "totalFlightCount";
    names[TotalFlightTimeSecRole] = "totalFlightTimeSec";
    names[TotalFlightHoursRole]   = "totalFlightHours";
    names[NotesRole]              = "notes";
    return names;
}

QVariantMap VehicleListModel::vehicleData(int row) const
{
    if (row < 0 || row >= m_vehicles.size())
        return {};
    return m_vehicles.at(row);
}

void VehicleListModel::reload(const QString &fromDate, const QString &toDate)
{
    QJsonArray arr = QJsonDocument::fromJson(
        DatabaseManager::instance().getAllVehiclesJson().toUtf8()).array();

    QDateTime from;
    QDateTime to;
    if (!fromDate.isEmpty())
        from = QDateTime::fromString(fromDate + QStringLiteral("T00:00:00"), Qt::ISODate);
    if (!toDate.isEmpty())
        to = QDateTime::fromString(toDate + QStringLiteral("T23:59:59"), Qt::ISODate);

    QList<QVariantMap> filtered;
    for (const QJsonValue &val : arr) {
        QJsonObject o = val.toObject();
        bool keep = true;
        if (from.isValid() || to.isValid()) {
            QDateTime lastSeen = QDateTime::fromString(o.value(QStringLiteral("lastSeen")).toString(),
                                                       QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (lastSeen.isValid()) {
                if (from.isValid() && lastSeen < from) keep = false;
                if (to.isValid() && lastSeen > to) keep = false;
            }
        }
        if (keep)
            filtered.append(o.toVariantMap());
    }

    beginResetModel();
    m_vehicles = filtered;
    endResetModel();
    qCDebug(vehicleListDebug) << "VehicleListModel: reloaded" << m_vehicles.size() << "vehicles";
    emit countChanged();
}