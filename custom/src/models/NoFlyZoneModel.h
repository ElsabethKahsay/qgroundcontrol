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
/// Automatic geometry/intersection logic lives in ZoneComplianceCheck (which
/// reads the loaded mission); this model handles CRUD, search filtering and
/// sorting, plus manual compliance records via submitCompliance() (stored in
/// zone_compliance_log for the audit trail).

class NoFlyZoneModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// Total number of zones currently loaded from the database.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

    /// Number of zones with active = 1 (drives the sidebar badge).
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)

    /// Current case-insensitive name filter ("" shows everything).
    Q_PROPERTY(QString nameFilter READ nameFilter WRITE setNameFilter NOTIFY nameFilterChanged)

    /// Sort mode: "name" (A→Z, default), "radius" (largest first),
    /// "reason" or "updated" (newest first).
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

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

    // ── Search / sort ────────────────────────────────────────────────
    Q_INVOKABLE void setNameFilter(const QString &filter);
    Q_INVOKABLE void setSortMode(const QString &mode);
    QString nameFilter() const { return _nameFilter; }
    QString sortMode() const { return _sortMode; }

    // ── Zone CRUD ────────────────────────────────────────────────────
    /// Creates a zone.  Returns the new row id, or -1 when blocked (empty or
    /// duplicate name) — in that case errorOccurred carries the reason.
    Q_INVOKABLE int createZone(const QString &name, const QString &description,
                               double lat, double lon, double radiusM,
                               const QString &reason);
    Q_INVOKABLE bool updateZone(int id, const QString &name,
                                const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, bool active);
    Q_INVOKABLE bool removeZone(int id);
    Q_INVOKABLE bool toggleZoneActive(int id);

    int activeCount() const;

    /// All zones with active = 1 straight from the DB cache.
    /// Used by ZoneComplianceCheck for route intersection testing.
    QList<QVariantMap> activeZones() const;

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
    void zonesChanged();               ///< zone set changed (CRUD / reload)
    void nameFilterChanged();
    void sortModeChanged();
    void complianceCheckedChanged();
    void complianceRecordsChanged();
    /// A CRUD operation was rejected (empty/duplicate name).  Shown in QML.
    void errorOccurred(const QString &message);
    /// Advisory only — a saved zone overlaps an existing active zone.
    void overlapWarning(const QString &message);

private:
    void _reloadCompliance();
    void _applyFilterSort();
    void _checkOverlapAfterSave(int id, double lat, double lon, double radiusM);
    QString nameFromId(int id) const;
    QList<QVariantMap> m_zones;            ///< all zones straight from the DB
    QList<QVariantMap> m_visibleZones;     ///< filtered + sorted view of m_zones
    QVariantList m_complianceRecords;
    QString _nameFilter;
    QString _sortMode = QStringLiteral("name");
    bool _complianceChecked = false;
};
