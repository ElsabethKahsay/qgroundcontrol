#include "FlightHistoryModel.h"
#include "DatabaseManager.h"

FlightHistoryModel::FlightHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FlightHistoryModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_flights.size();
}

QVariant FlightHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_flights.size())
        return {};

    const QVariantMap &f = m_flights.at(index.row());
    switch (role) {
    case FlightIdRole:      return f.value(QStringLiteral("id"));
    case DateRole:          return f.value(QStringLiteral("created_at"));
    case OperatorNameRole:  return f.value(QStringLiteral("operator_name"));
    case ModeRole:          return f.value(QStringLiteral("mode"));
    case PurposeRole:       return f.value(QStringLiteral("purpose"));
    case LocationRole:      return f.value(QStringLiteral("location"));
    case DurationRole:      return f.value(QStringLiteral("duration_sec"));
    case PrePassRateRole:   return f.value(QStringLiteral("pre_pass_rate"));
    case HasEventsRole:     return f.value(QStringLiteral("has_events"));
    case HasPostFlightRole: return f.value(QStringLiteral("has_post_flight"));
    }
    return {};
}

QHash<int, QByteArray> FlightHistoryModel::roleNames() const
{
    return {
        { FlightIdRole,      "flightId" },
        { DateRole,          "date" },
        { OperatorNameRole,  "operatorName" },
        { ModeRole,          "mode" },
        { PurposeRole,       "purpose" },
        { LocationRole,      "location" },
        { DurationRole,      "duration" },
        { PrePassRateRole,   "prePassRate" },
        { HasEventsRole,     "hasEvents" },
        { HasPostFlightRole, "hasPostFlight" },
    };
}

void FlightHistoryModel::loadForVehicle(int vehicleId)
{
    beginResetModel();
    m_flights = DatabaseManager::instance().getFlightsForVehicle(vehicleId, 50);
    endResetModel();
    emit countChanged();
}

QVariantMap FlightHistoryModel::getFlightData(int flightId) const
{
    return DatabaseManager::instance().getFlightById(flightId);
}
