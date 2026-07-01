#include "MaintenanceTracker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Config.h"
#include "DatabaseManager.h"

MaintenanceTracker::MaintenanceTracker(QObject *parent)
    : QObject(parent)
{
    connect(&m_tickTimer, &QTimer::timeout, this, &MaintenanceTracker::onTick);
    refreshComponents();
}

void MaintenanceTracker::addComponent(const QString &name, const QString &type,
                                       double maxHours, int maxCycles)
{
    DatabaseManager::instance().addComponent(name, type, maxHours, maxCycles);
    refreshComponents();
}

void MaintenanceTracker::removeComponent(int id)
{
    DatabaseManager::instance().deleteComponent(id);
    refreshComponents();
}

void MaintenanceTracker::resetComponent(int id, const QString &notes)
{
    DatabaseManager::instance().resetComponentMaintenance(id, notes);
    refreshComponents();
}

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
                }
                refreshComponents();
                checkThresholds();
            }
        }
    }
}

void MaintenanceTracker::onTick()
{
    // During flight, periodically check for threshold alerts
    checkThresholds();
}

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
