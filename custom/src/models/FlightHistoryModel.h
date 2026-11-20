#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QList>

/// Flight History list model. Loads flights from DatabaseManager with
/// filter / sort / pagination applied on reload().  Designed to back the
/// flight history page list and detail panel.
class FlightHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterFromDate READ filterFromDate WRITE setFilterFromDate NOTIFY filterFromDateChanged)
    Q_PROPERTY(QString filterToDate   READ filterToDate   WRITE setFilterToDate   NOTIFY filterToDateChanged)
    Q_PROPERTY(QString filterVehicleKey READ filterVehicleKey WRITE setFilterVehicleKey NOTIFY filterVehicleKeyChanged)
    Q_PROPERTY(int     filterOperatorId READ filterOperatorId WRITE setFilterOperatorId NOTIFY filterOperatorIdChanged)
    Q_PROPERTY(QString filterMode     READ filterMode     WRITE setFilterMode     NOTIFY filterModeChanged)
    Q_PROPERTY(QString filterSearch   READ filterSearch   WRITE setFilterSearch   NOTIFY filterSearchChanged)
    Q_PROPERTY(QString sortBy         READ sortBy         WRITE setSortBy         NOTIFY sortByChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int pageSize   READ pageSize   CONSTANT)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)

public:
    enum Role {
        FlightIdRole = Qt::UserRole + 1,
        DateRole,
        OperatorNameRole,
        VehicleNameRole,
        ModeRole,
        PurposeRole,
        LocationRole,
        DurationSecRole,
        DurationStrRole,
        PrePassRateRole,
        AnomalyCountRole,
        ArmedAtRole,
        IsCompleteRole,
        MaxAltRole,
        MinBattRole,
    };
    Q_ENUM(Role)

    explicit FlightHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void prevPage();
    Q_INVOKABLE void goToPage(int page);
    /// Entry point used by the flight history page: sets the vehicle key
    /// filter and reloads. An empty key shows every vehicle.
    Q_INVOKABLE void loadForVehicle(const QString &vehicleKey) { setFilterVehicleKey(vehicleKey); }
    /// Full flight record + sub-records (checks, events, tests, compliance)
    /// assembled for the detail panel.
    Q_INVOKABLE QVariantMap getFlightDetail(int flightId);
    /// Backwards-compatible alias: returns just the flights row.
    Q_INVOKABLE QVariantMap getFlightData(int flightId);

    int totalCount() const { return m_totalCount; }
    int pageSize() const { return m_pageSize; }
    int currentPage() const { return m_currentPage; }
    QString filterFromDate() const { return m_filterFromDate; }
    QString filterToDate() const { return m_filterToDate; }
    QString filterVehicleKey() const { return m_filterVehicleKey; }
    int filterOperatorId() const { return m_filterOperatorId; }
    QString filterMode() const { return m_filterMode; }
    QString filterSearch() const { return m_filterSearch; }
    QString sortBy() const { return m_sortBy; }
    QVariantMap stats() const { return m_stats; }

    // Setters (WRITE props) — each triggers reload().
    Q_INVOKABLE void setFilterFromDate(const QString &v) { m_filterFromDate = v; emit filterFromDateChanged(); reload(); }
    Q_INVOKABLE void setFilterToDate(const QString &v) { m_filterToDate = v; emit filterToDateChanged(); reload(); }
    Q_INVOKABLE void setFilterVehicleKey(const QString &v) { m_filterVehicleKey = v; emit filterVehicleKeyChanged(); goToPage(0); reload(); }
    Q_INVOKABLE void setFilterOperatorId(int v) { m_filterOperatorId = v; emit filterOperatorIdChanged(); reload(); }
    Q_INVOKABLE void setFilterMode(const QString &v) { m_filterMode = v; emit filterModeChanged(); reload(); }
    Q_INVOKABLE void setFilterSearch(const QString &v) { m_filterSearch = v; emit filterSearchChanged(); reload(); }
    Q_INVOKABLE void setSortBy(const QString &v) { m_sortBy = v; emit sortByChanged(); reload(); }

signals:
    void totalCountChanged();
    void currentPageChanged();
    void statsChanged();
    void filterFromDateChanged();
    void filterToDateChanged();
    void filterVehicleKeyChanged();
    void filterOperatorIdChanged();
    void filterModeChanged();
    void filterSearchChanged();
    void sortByChanged();

private:
    QString _durationString(int sec) const;
    int _passRate(const QVariantMap &f) const;

    QList<QVariantMap> m_flights;
    QVariantMap m_stats;
    int m_totalCount = 0;
    int m_pageSize = 50;
    int m_currentPage = 0;

    QString m_filterFromDate;
    QString m_filterToDate;
    QString m_filterVehicleKey;
    int     m_filterOperatorId = 0;
    QString m_filterMode;
    QString m_filterSearch;
    QString m_sortBy = QStringLiteral("date_desc");
};