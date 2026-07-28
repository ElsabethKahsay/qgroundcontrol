#include "MaintenanceTracker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Config.h"
#include "DatabaseManager.h"

/// Connects the periodic tick timer and loads initial component data from the database.
MaintenanceTracker::MaintenanceTracker(QObject *parent)
    : QObject(parent)
{
    connect(&m_tickTimer, &QTimer::timeout, this, &MaintenanceTracker::onTick);
    refreshComponents();
}

/** @brief Add a new component to track, then refresh the component list. */
void MaintenanceTracker::addComponent(const QString &name, const QString &type,
                                       double maxHours, int maxCycles)
{
    DatabaseManager::instance().addComponent(name, type, maxHours, maxCycles);
    refreshComponents();
}

/// Delete a component from the database and refresh the local list.
void MaintenanceTracker::removeComponent(int id)
{
    DatabaseManager::instance().deleteComponent(id);
    refreshComponents();
}

/// Zero out a component's hour and cycle counters, then refresh.
void MaintenanceTracker::resetComponent(int id, const QString &notes)
{
    DatabaseManager::instance().resetComponentMaintenance(id, notes);
    refreshComponents();
}

/** @brief Reload the component list from DatabaseManager and notify QML. */
void MaintenanceTracker::refreshComponents()
{
    QString json = DatabaseManager::instance().listComponentsJson();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QVariantList list;
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array())
            list.append(v.toObject().toVariantMap());
    }
    m_components = list;
    emit componentsChanged();
}

/**
 * @brief Handle arm/disarm state transitions.
 *
 * On arm: starts a timer to track elapsed armed time and a periodic
 * tick for in-flight threshold checks.
 * On disarm: calculates total elapsed armed time, distributes it to
 * all tracked components as hours, increments their cycle counts,
 * and checks for threshold violations.
 */
void MaintenanceTracker::setArmed(bool armed)
{
    if (m_armed == armed) return;
    m_armed = armed;
    emit armedChanged();

    if (armed) {
        m_armedTimer.start();
        m_tickTimer.start(60000); // tick every 60 seconds while armed
    } else {
        m_tickTimer.stop();
        // Add elapsed seconds to all components
        if (m_armedTimer.isValid()) {
            qint64 elapsedSec = m_armedTimer.elapsed() / 1000;
            double elapsedHours = elapsedSec / 3600.0;
            if (elapsedHours > 0.001) {
                for (const QVariant &cv : m_components) {
                    QVariantMap c = cv.toMap();
                    int id = c.value(QStringLiteral("id")).toInt();
                    double cur = c.value(QStringLiteral("currentHours")).toDouble();
                    DatabaseManager::instance().updateComponentHours(id, cur + elapsedHours);
                    DatabaseManager::instance().incrementComponentCycle(id);
                }
                refreshComponents();
                checkThresholds();
            }
        }
    }
}

/// Periodic threshold check while armed (called every 60 seconds by m_tickTimer).
void MaintenanceTracker::onTick()
{
    checkThresholds();
}

/**
 * @brief Check all components against their hour and cycle limits.
 *
 * Computes usage as a percentage (current / max * 100) and emits
 * maintenanceWarning or maintenanceCritical signals for any component
 * that exceeds the thresholds from Config.h.
 */
void MaintenanceTracker::checkThresholds()
{
    for (const QVariant &cv : m_components) {
        QVariantMap c = cv.toMap();
        int id = c.value(QStringLiteral("id")).toInt();
        QString name = c.value(QStringLiteral("name")).toString();
        double maxH = c.value(QStringLiteral("maxHours")).toDouble();
        double curH = c.value(QStringLiteral("currentHours")).toDouble();
        int maxC = c.value(QStringLiteral("maxCycles")).toInt();
        int curC = c.value(QStringLiteral("currentCycles")).toInt();

        if (maxH > 0) {
            int pct = qBound(0, static_cast<int>(curH / maxH * 100.0), 200);
            if (pct >= kMaintCriticalPercent)
                emit maintenanceCritical(id, name, pct);
            else if (pct >= kMaintWarningPercent)
                emit maintenanceWarning(id, name, pct);
        }
        if (maxC > 0) {
            int pct = qBound(0, static_cast<int>(static_cast<double>(curC) / maxC * 100.0), 200);
            if (pct >= kMaintCriticalPercent)
                emit maintenanceCritical(id, name, pct);
            else if (pct >= kMaintWarningPercent)
                emit maintenanceWarning(id, name, pct);
        }
    }
}
