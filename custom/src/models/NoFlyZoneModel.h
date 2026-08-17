#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

/// @file NoFlyZoneModel.h
///
/// QML-exposed list model of known restricted airspace zones (no-fly zones).
///
/// This is a persistent organizational knowledge base, NOT a geofence.
/// There is deliberately no automatic geometry / intersection logic in this
/// model — compliance is recorded manually by the operator via
/// submitCompliance() (stored in zone_compliance_log for the audit trail).

class NoFlyZoneModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

    /// True once the operator has submitted the manual compliance form for the
    /// current session.  Used to mark a mission as "Compliance Checked".
    Q_PROPERTY(bool complianceChecked READ complianceChecked NOTIFY complianceCheckedChanged)

    /// Latest compliance records (from zone_compliance_log), newest first.
    Q_PROPERTY(QVariantList complianceRecords READ complianceRecords NOTIFY complianceRecordsChanged)

public:
    enum Role {
        ZoneIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        LatitudeRole,
        LongitudeRole,
        RadiusMRole,
        ReasonRole,
        ActiveRole,
        CreatedAtRole,
        UpdatedAtRole,
        CreatedByNameRole,
    };
    Q_ENUM(Role)

    explicit NoFlyZoneModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Reloads zones and compliance records from the database.
    Q_INVOKABLE void reload();

    // ── Zone CRUD ────────────────────────────────────────────────────
    Q_INVOKABLE int createZone(const QString &name, const QString &description,
                               double lat, double lon, double radiusM,
                               const QString &reason);
    Q_INVOKABLE bool updateZone(int id, const QString &name,
                                const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, bool active);
    Q_INVOKABLE bool removeZone(int id);
    Q_INVOKABLE bool toggleZoneActive(int id);

    // ── Manual compliance ────────────────────────────────────────────
    /// Submits the manual compliance form for the current session.  flight_id
    /// and operator are taken automatically from FlightSession/OperatorManager.
    /// Marks the mission as "Compliance Checked" on success.
    Q_INVOKABLE bool submitCompliance(const QString &result,
                                      const QString &notes,
                                      const QString &overrideReason);

    /// Clears the compliance-checked flag for a new session.
    Q_INVOKABLE void resetCompliance();

    bool complianceChecked() const { return _complianceChecked; }
    QVariantList complianceRecords() const { return m_complianceRecords; }

signals:
    void countChanged();
    void complianceCheckedChanged();
    void complianceRecordsChanged();

private:
    void _reloadCompliance();

    QList<QVariantMap> m_zones;
    QVariantList m_complianceRecords;
    bool _complianceChecked = false;
};
