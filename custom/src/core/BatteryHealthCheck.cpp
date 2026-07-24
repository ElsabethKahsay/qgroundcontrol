#include "BatteryHealthCheck.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "DatabaseManager.h"
#include "VehicleProfileManager.h"

BatteryHealthCheck::BatteryHealthCheck(VehicleProfileManager *profileManager,
                                       double capacityWarningPct,
                                       double sagWarningV,
                                       QObject *parent)
    : AbstractCheck(QStringLiteral("battery_health_trend"),
                    QStringLiteral("Battery Health Trend"),
                    CheckCategory::Power,
                    CheckType::Auto,
                    false,   // mandatory = false (informational)
                    false,   // canOverride = false (trend is factual)
                    parent)
    , m_profileManager(profileManager)
    , m_capacityWarningPct(capacityWarningPct)
    , m_sagWarningV(sagWarningV)
{
}

// Warn if capacity retained < threshold or avg voltage sag > threshold; pass if healthy.
void BatteryHealthCheck::evaluate()
{
    if (!m_profileManager) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No profile manager"));
        return;
    }

    QString serial = m_profileManager->currentBatterySerial();
    if (serial.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No battery identified"));
        return;
    }

    QString trendJson = DatabaseManager::instance().getBatteryHealthTrend(serial);
    if (trendJson.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No cycle data yet for this battery"));
        return;
    }

    QJsonObject trend = QJsonDocument::fromJson(trendJson.toUtf8()).object();
    double retainedPct = trend.value(QStringLiteral("capacityRetainedPct")).toDouble(100.0);
    double avgSag = trend.value(QStringLiteral("averageVoltageSagV")).toDouble(0.0);
    int totalCycles = trend.value(QStringLiteral("totalCycles")).toInt(0);

    if (retainedPct < m_capacityWarningPct && totalCycles > 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Battery retained %1% capacity (threshold %2%) after %3 cycles")
                      .arg(retainedPct, 0, 'f', 1)
                      .arg(m_capacityWarningPct, 0, 'f', 1)
                      .arg(totalCycles));
    } else if (avgSag > m_sagWarningV && totalCycles > 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Battery voltage sag %1V (threshold %2V) over %3 cycles")
                      .arg(avgSag, 0, 'f', 3)
                      .arg(m_sagWarningV, 0, 'f', 3)
                      .arg(totalCycles));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Battery healthy: %1% capacity retained, %2V sag over %3 cycles")
                      .arg(retainedPct, 0, 'f', 1)
                      .arg(avgSag, 0, 'f', 3)
                      .arg(totalCycles));
    }
}

QString BatteryHealthCheck::getRationale() const
{
    return QStringLiteral("Tracks battery capacity retention and voltage sag across cycles "
                          "to identify degraded batteries before flight.");
}

QStringList BatteryHealthCheck::getFixSteps() const
{
    return {
        QStringLiteral("Replace battery if capacity drops below %1%").arg(m_capacityWarningPct, 0, 'f', 0),
        QStringLiteral("Check for swollen cells or physical damage"),
        QStringLiteral("Verify battery is charged to full before flight"),
    };
}

QString BatteryHealthCheck::getThreshold() const
{
    return QStringLiteral("Capacity > %1%, Sag < %2V").arg(m_capacityWarningPct, 0, 'f', 0).arg(m_sagWarningV, 0, 'f', 2);
}

QString BatteryHealthCheck::getCurrentValueString() const
{
    if (!m_profileManager) return QStringLiteral("No data");
    QString serial = m_profileManager->currentBatterySerial();
    if (serial.isEmpty()) return QStringLiteral("Unknown battery");
    return serial;
}


