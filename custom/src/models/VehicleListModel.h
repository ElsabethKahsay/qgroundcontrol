#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

/// @file VehicleListModel.h
///
/// QML-exposed list model of every vehicle in the registry (from the `vehicles`
/// table).  Populated from DatabaseManager::getAllVehiclesJson() with optional
/// last-seen date filtering, and used by the Vehicles analyze page to render
/// the vehicle rows without JSON parsing in QML.

class VehicleListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        DeviceUidRole = Qt::UserRole + 1,
        FriendlyNameRole,
        AutopilotTypeRole,
        AirframeTypeRole,
        VehicleTypeNameRole,
        FirmwareVersionRole,
        BoardVersionRole,
        HardwareUidRole,
        FingerprintRole,
        FingerprintSourceRole,
        SysidRole,
        CompidRole,
        FrameClassRole,
        MotorCountRole,
        FirstSeenRole,
        LastSeenRole,
        TotalFlightCountRole,
        TotalFlightTimeSecRole,
        TotalFlightHoursRole,
        NotesRole,
    };
    Q_ENUM(Role)

    explicit VehicleListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Reload all vehicles from the database (optionally filtered by last-seen
    /// date range, both in "YYYY-MM-DD" form; empty = unbounded).
    Q_INVOKABLE void reload(const QString &fromDate = QString(), const QString &toDate = QString());

    /// Return the full QVariantMap for a row (empty map when out of range).
    Q_INVOKABLE QVariantMap vehicleData(int row) const;

signals:
    void countChanged();

private:
    QList<QVariantMap> m_vehicles;
};
