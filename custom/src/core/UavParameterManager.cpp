#include "UavParameterManager.h"

#include <QDebug>

// PX4 ↔ ArduPilot parameter name mapping table
const QMap<QString, QMap<QString, QString>> UavParameterManager::s_paramMappings = {
    // Power
    {{"BAT1_A_PER_V"},    {{"PX4", "BAT1_A_PER_V"},    {"ArduPilot", "BATT_AMP_PERVLT"}}},
    {{"BAT1_V_DIV"},      {{"PX4", "BAT1_V_DIV"},      {"ArduPilot", "BATT_VOLT_MULT"}}},
    {{"BATT_MONITOR"},    {{"PX4", "BATT_MONITOR"},    {"ArduPilot", "BATT_MONITOR"}}},
    {{"BATT_CAPACITY"},   {{"PX4", "BATT_CAPACITY"},   {"ArduPilot", "BATT_CAPACITY"}}},
    {{"BATT_CELL_COUNT"}, {{"PX4", "BATT_CELL_COUNT"}, {"ArduPilot", "BATT_CELL_COUNT"}}},
    {{"BAT_LOW_THR"},     {{"PX4", "BAT_LOW_THR"},     {"ArduPilot", "BATT_LOW_VOLT"}}},
    {{"BAT_CRIT_THR"},    {{"PX4", "BAT_CRIT_THR"},    {"ArduPilot", "BATT_CRIT_VOLT"}}},
    {{"BAT_EMERGEN_THR"}, {{"PX4", "BAT_EMERGEN_THR"}, {"ArduPilot", "BATT_EMERGE_VOLT"}}},
    // Navigation
    {{"GPS_GNSSMODE"},    {{"PX4", "GPS_GNSSMODE"},    {"ArduPilot", "GPS_TYPE"}}},
    {{"GPS_HDOP_GOOD"},   {{"PX4", "GPS_HDOP_GOOD"},   {"ArduPilot", "GPS_HDOP"}}},
    {{"EKF2_ENABLE"},     {{"PX4", "EKF2_ENABLE"},     {"ArduPilot", "EK2_ENABLE"}}},
    {{"EKF3_ENABLE"},     {{"PX4", "EKF3_ENABLE"},     {"ArduPilot", "EK3_ENABLE"}}},
    // Safety
    {{"FENCE_ENABLE"},    {{"PX4", "FENCE_ENABLE"},    {"ArduPilot", "FENCE_ENABLE"}}},
    {{"FENCE_TYPE"},      {{"PX4", "FENCE_TYPE"},      {"ArduPilot", "FENCE_TYPE"}}},
    {{"FENCE_ACTION"},    {{"PX4", "FENCE_ACTION"},    {"ArduPilot", "FENCE_ACTION"}}},
    {{"FENCE_ALT_MAX"},   {{"PX4", "FENCE_ALT_MAX"},   {"ArduPilot", "FENCE_ALT_MAX"}}},
    {{"RTL_RETURN_ALT"},  {{"PX4", "RTL_RETURN_ALT"},  {"ArduPilot", "RTL_RETURN_ALT"}}},
    {{"RTL_DESCEND_ALT"}, {{"PX4", "RTL_DESCEND_ALT"}, {"ArduPilot", "RTL_DESCEND_ALT"}}},
    {{"RTL_ALT"},         {{"PX4", "RTL_ALT"},         {"ArduPilot", "RTL_ALT"}}},
    // Arming
    {{"ARMING_CHECK"},    {{"PX4", "ARMING_CHECK"},    {"ArduPilot", "ARMING_CHECK"}}},
    {{"COM_ARM_ENABLE"},  {{"PX4", "COM_ARM_ENABLE"},  {"ArduPilot", "ARMING_ENABLE"}}},
    {{"SYS_HITL"},        {{"PX4", "SYS_HITL"},        {"ArduPilot", "SIM_GPS_TYPE"}}},
};

UavParameterManager::UavParameterManager(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10000);
    connect(&m_timeout, &QTimer::timeout, this, &UavParameterManager::onTimeout);
}

void UavParameterManager::setWatchlist(const QSet<QString> &params)
{
    m_watchlist = params;
    m_received.clear();
    m_status = Loading;
    m_timeout.start();
    emit statusChanged();
}

void UavParameterManager::notifyParamReceived(const QString &name)
{
    if (!m_watchlist.contains(name))
        return;

    m_received.insert(name);
    reevaluate();
}

void UavParameterManager::setAutopilotType(AutopilotType type)
{
    m_autopilotType = type;
}

QString UavParameterManager::mapParamName(const QString &name, AutopilotType from, AutopilotType to)
{
    if (from == to) return name;

    QString fromStr = (from == PX4) ? QStringLiteral("PX4") : QStringLiteral("ArduPilot");
    QString toStr   = (to   == PX4) ? QStringLiteral("PX4") : QStringLiteral("ArduPilot");

    // Forward lookup: name is a canonical key
    auto it = s_paramMappings.constFind(name);
    if (it != s_paramMappings.constEnd()) {
        auto toIt = it->constFind(toStr);
        if (toIt != it->constEnd())
            return toIt.value();
    }

    // Reverse lookup: name might be a PX4 or ArduPilot name
    for (auto mapIt = s_paramMappings.constBegin(); mapIt != s_paramMappings.constEnd(); ++mapIt) {
        auto fromIt = mapIt->constFind(fromStr);
        if (fromIt != mapIt->constEnd() && fromIt.value() == name) {
            auto toIt = mapIt->constFind(toStr);
            if (toIt != mapIt->constEnd())
                return toIt.value();
            return mapIt.key(); // Fall back to canonical name
        }
    }

    return name;
}

QString UavParameterManager::toLocalName(const QString &name) const
{
    if (m_autopilotType == Generic) return name;

    // Try mapping from "canonical" (first key) to local autopilot name
    QString target = (m_autopilotType == PX4) ? QStringLiteral("PX4") : QStringLiteral("ArduPilot");

    auto it = s_paramMappings.constFind(name);
    if (it != s_paramMappings.constEnd()) {
        auto targetIt = it->constFind(target);
        if (targetIt != it->constEnd()) {
            return targetIt.value();
        }
    }
    return name;
}

void UavParameterManager::storeParam(const QString &name, float value)
{
    QString local = toLocalName(name);
    m_cache[local] = value;
}

float UavParameterManager::paramValue(const QString &name, float defaultVal) const
{
    QString local = toLocalName(name);
    return m_cache.value(local, defaultVal);
}

void UavParameterManager::reset()
{
    m_received.clear();
    m_cache.clear();
    m_timeout.stop();
    m_status = Loading;
    emit statusChanged();
}

void UavParameterManager::onTimeout()
{
    m_status = Fallback;
    m_timeout.stop();
    emit statusChanged();
    emit fallback(QStringLiteral("Using cached defaults \u2014 verify thresholds"));
}

void UavParameterManager::reevaluate()
{
    if (m_received.size() >= m_watchlist.size()) {
        m_status = Ready;
        m_timeout.stop();
        emit statusChanged();
        emit ready();
    }
}
