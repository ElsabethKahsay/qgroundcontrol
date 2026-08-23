#include "FlightHistoryModel.h"
#include "DatabaseManager.h"

#include <QDateTime>
#include <QVariantList>

FlightHistoryModel::FlightHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FlightHistoryModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_flights.size();
}

int FlightHistoryModel::_passRate(const QVariantMap &f) const
{
    const int pass = f.value(QStringLiteral("check_pass_count")).toInt();
    const int fail = f.value(QStringLiteral("check_fail_count")).toInt();
    const int warn = f.value(QStringLiteral("check_warn_count")).toInt();
    const int total = pass + fail + warn;
    if (total <= 0) return 100;
    return qRound((pass * 100.0) / total);
}

QString FlightHistoryModel::_durationString(int sec) const
{
    if (sec <= 0) return QStringLiteral("0s");
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    if (h > 0) return QStringLiteral("%1h %2m").arg(h).arg(m);
    if (m > 0) return QStringLiteral("%1m %2s").arg(m).arg(s);
    return QStringLiteral("%1s").arg(s);
}

QVariant FlightHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_flights.size())
        return {};

    const QVariantMap &f = m_flights.at(index.row());
    switch (role) {
    case FlightIdRole:
        return f.value(QStringLiteral("id"));
    case DateRole: {
        QDateTime dt = QDateTime::fromString(
            f.value(QStringLiteral("started_at")).toString(),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!dt.isValid())
            dt = QDateTime::fromString(
                f.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
        return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd")) : QString();
    }
    case OperatorNameRole:
        return f.value(QStringLiteral("operator_name"));
    case VehicleNameRole:
        return f.value(QStringLiteral("vehicle_name"));
    case ModeRole:
        return f.value(QStringLiteral("mode"));
    case PurposeRole:
        return f.value(QStringLiteral("purpose"));
    case LocationRole:
        return f.value(QStringLiteral("location"));
    case DurationSecRole:
        return f.value(QStringLiteral("duration_sec")).toInt();
    case DurationStrRole:
        return _durationString(f.value(QStringLiteral("duration_sec")).toInt());
    case PrePassRateRole:
        return _passRate(f);
    case AnomalyCountRole:
        return f.value(QStringLiteral("anomaly_count")).toInt();
    case ArmedAtRole:
        return f.value(QStringLiteral("armed_at")).toString();
    case IsCompleteRole:
        return f.value(QStringLiteral("pre_checklist_complete")).toInt() > 0
            && f.value(QStringLiteral("post_checklist_complete")).toInt() > 0;
    case MaxAltRole:
        return f.value(QStringLiteral("max_altitude_m")).toDouble();
    case MinBattRole:
        return f.value(QStringLiteral("min_battery_v")).toDouble();
    }
    return {};
}

QHash<int, QByteArray> FlightHistoryModel::roleNames() const
{
    return {
        { FlightIdRole,      "flightId" },
        { DateRole,          "date" },
        { OperatorNameRole,  "operatorName" },
        { VehicleNameRole,   "vehicleName" },
        { ModeRole,          "mode" },
        { PurposeRole,       "purpose" },
        { LocationRole,      "location" },
        { DurationSecRole,   "durationSec" },
        { DurationStrRole,   "durationStr" },
        { PrePassRateRole,   "prePassRate" },
        { AnomalyCountRole,  "anomalyCount" },
        { ArmedAtRole,       "armedAt" },
        { IsCompleteRole,    "isComplete" },
        { MaxAltRole,        "maxAlt" },
        { MinBattRole,       "minBatt" },
    };
}

void FlightHistoryModel::reload()
{
    beginResetModel();
    DatabaseManager &db = DatabaseManager::instance();
    const QVariantMap result = db.queryFlights(
        m_currentPage, m_filterFromDate, m_filterToDate,
        m_filterVehicleId, m_filterOperatorId, m_filterMode,
        m_filterSearch, m_sortBy, m_pageSize);
    const QVariantList rows = result.value(QStringLiteral("rows")).toList();
    m_flights.clear();
    m_flights.reserve(rows.size());
    for (const QVariant &v : rows)
        m_flights.append(v.toMap());
    const int newTotal = result.value(QStringLiteral("totalCount")).toInt();
    if (newTotal != m_totalCount) {
        m_totalCount = newTotal;
        emit totalCountChanged();
    }
    endResetModel();

    const QVariantMap newStats = db.getFlightStats(
        m_filterFromDate, m_filterToDate, m_filterVehicleId, m_filterMode);
    if (newStats != m_stats) {
        m_stats = newStats;
        emit statsChanged();
    }
}

void FlightHistoryModel::nextPage()
{
    if ((m_currentPage + 1) * m_pageSize < m_totalCount) {
        m_currentPage++;
        emit currentPageChanged();
        reload();
    }
}

void FlightHistoryModel::prevPage()
{
    if (m_currentPage > 0) {
        m_currentPage--;
        emit currentPageChanged();
        reload();
    }
}

void FlightHistoryModel::goToPage(int page)
{
    if (page < 0 || page == m_currentPage)
        return;
    m_currentPage = page;
    emit currentPageChanged();
    reload();
}

QVariantMap FlightHistoryModel::getFlightDetail(int flightId)
{
    DatabaseManager &db = DatabaseManager::instance();
    QVariantMap detail;

    auto asList = [](const QList<QVariantMap> &records) {
        QVariantList list;
        list.reserve(records.size());
        for (const QVariantMap &r : records)
            list.append(r);
        return list;
    };

    detail[QStringLiteral("flight")]      = db.getFlightById(flightId);
    detail[QStringLiteral("checks_pre")]  = asList(db.getCheckResultsForFlight(flightId, false));
    detail[QStringLiteral("checks_post")] = asList(db.getCheckResultsForFlight(flightId, true));
    detail[QStringLiteral("events")]      = asList(db.getTelemetryEventsForFlight(flightId));
    detail[QStringLiteral("handovers")]   = asList(db.getHandoverEventsForFlight(flightId));
    detail[QStringLiteral("surface_tests")] = asList(db.getSurfaceTestsForFlight(flightId));
    detail[QStringLiteral("compliance")]  = asList(db.getComplianceForFlight(flightId));

    const QVariantMap flight = db.getFlightById(flightId);
    const int vehicleSysId = flight.value(QStringLiteral("vehicle_id")).toInt();
    detail[QStringLiteral("motor_tests")] = asList(db.getMotorTestsForVehicle(vehicleSysId));
    return detail;
}

QVariantMap FlightHistoryModel::getFlightData(int flightId)
{
    return DatabaseManager::instance().getFlightById(flightId);
}