#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <QList>

class FlightHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        FlightIdRole = Qt::UserRole + 1,
        DateRole,
        OperatorNameRole,
        ModeRole,
        PurposeRole,
        LocationRole,
        DurationRole,
        PrePassRateRole,
        HasEventsRole,
        HasPostFlightRole,
    };
    Q_ENUM(Role)

    explicit FlightHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void loadForVehicle(int vehicleId);
    Q_INVOKABLE QVariantMap getFlightData(int flightId) const;

signals:
    void countChanged();

private:
    QList<QVariantMap> m_flights;
};
