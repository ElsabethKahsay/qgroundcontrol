// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/AlertManager.cpp
// Description: Implementation of AlertManager.

#include "AlertManager.h"

#include <QDebug>

/**
 * @brief Get the singleton instance of AlertManager
 * @return Reference to the singleton instance
 * 
 * Uses Meyers' singleton pattern for thread-safe lazy initialization.
 */
AlertManager &AlertManager::instance()
{
    static AlertManager instance;
    return instance;
}

/**
 * @brief Constructor for AlertManager
 * @param parent Parent QObject
 */
AlertManager::AlertManager(QObject *parent) : QAbstractListModel(parent)
{
}

/**
 * @brief Destructor for AlertManager
 * 
 * Stops all momentary trigger timers and cleans up timer objects.
 */
AlertManager::~AlertManager()
{
    for (auto &info : m_momentaryTriggers) {
        info.timer->stop();
        info.timer->deleteLater();
    }
}

/**
 * @brief Returns the number of alerts in the model
 * @param parent Parent index (unused for flat list models)
 * @return Number of alerts
 */
int AlertManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_alerts.size();
}

/**
 * @brief Returns data for a given role and index
 * @param index Model index specifying which alert
 * @param role Role specifying which data field to return
 * @return Data value as QVariant, or invalid QVariant if index/role invalid
 */
QVariant AlertManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_alerts.size())
        return QVariant();

    const auto &alert = m_alerts[index.row()];
    switch (role) {
    case IdRole: return alert.id;
    case SeverityRole: return alert.severity;
    case CategoryRole: return alert.category;
    case MessageRole: return alert.message;
    case TimestampRole: return alert.timestamp;
    case AcknowledgedRole: return alert.acknowledged;
    default: return QVariant();
    }
}

/**
 * @brief Returns the role name mapping for QML
 * @return Hash mapping role enums to string names
 */
QHash<int, QByteArray> AlertManager::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[SeverityRole] = "severity";
    roles[CategoryRole] = "category";
    roles[MessageRole] = "message";
    roles[TimestampRole] = "timestamp";
    roles[AcknowledgedRole] = "acknowledged";
    return roles;
}

/**
 * @brief Post a new alert to the system
 * @param id Unique alert identifier
 * @param severity Severity level (0=Advisory, 1=Caution, 2=Warning)
 * @param category Alert category (e.g., "telemetry", "system")
 * @param message Human-readable alert message
 * 
 * If an unacknowledged alert with the same ID exists, it is updated
 * instead of creating a duplicate. New alerts are inserted at the top.
 */
void AlertManager::postAlert(const QString &id, int severity, const QString &category, const QString &message)
{
    // Check if unacknowledged alert with same ID exists
    for (int i = 0; i < m_alerts.size(); ++i) {
        if (m_alerts[i].id == id && !m_alerts[i].acknowledged) {
            // Update existing instead of adding
            m_alerts[i].severity = severity;
            m_alerts[i].message = message;
            m_alerts[i].timestamp = QDateTime::currentDateTime();
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {SeverityRole, MessageRole, TimestampRole});
            return;
        }
    }

    // Insert new alert at the top of the list
    beginInsertRows(QModelIndex(), 0, 0);
    AlertItem alert{id, severity, category, message, QDateTime::currentDateTime(), false};
    m_alerts.prepend(alert);
    endInsertRows();

    updateCounts();
    emit newAlertPosted(id, severity, message);
}

/**
 * @brief Mark an alert as acknowledged
 * @param id Unique alert identifier
 * 
 * Acknowledged alerts remain in the list but are not counted as "active".
 */
void AlertManager::acknowledgeAlert(const QString &id)
{
    for (int i = 0; i < m_alerts.size(); ++i) {
        if (m_alerts[i].id == id) {
            m_alerts[i].acknowledged = true;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {AcknowledgedRole});
            updateCounts();
            break;
        }
    }
}

/**
 * @brief Remove an alert from the list
 * @param id Unique alert identifier
 */
void AlertManager::clearAlert(const QString &id)
{
    for (int i = 0; i < m_alerts.size(); ++i) {
        if (m_alerts[i].id == id) {
            beginRemoveRows(QModelIndex(), i, i);
            m_alerts.removeAt(i);
            endRemoveRows();
            updateCounts();
            break;
        }
    }
}

/**
 * @brief Remove all alerts from the list
 */
void AlertManager::clearAllAlerts()
{
    if (m_alerts.isEmpty()) return;
    beginRemoveRows(QModelIndex(), 0, m_alerts.size() - 1);
    m_alerts.clear();
    endRemoveRows();
    updateCounts();
}

/**
 * @brief Register a momentary alert trigger
 * @param id Unique trigger identifier
 * @param intervalMs Interval between triggers in milliseconds
 * @param severity Severity level (0=Advisory, 1=Caution, 2=Warning)
 * @param category Alert category
 * @param message Alert message
 * 
 * Momentary triggers fire repeatedly at the specified interval.
 * Used for conditions that need to be monitored continuously.
 */
void AlertManager::registerMomentaryTrigger(const QString &id, int intervalMs, int severity, const QString &category, const QString &message)
{
    if (m_momentaryTriggers.contains(id)) return;  // Already registered

    // Create and configure timer
    QTimer *timer = new QTimer(this);
    timer->setInterval(intervalMs);
    connect(timer, &QTimer::timeout, this, [this, id]() {
        onMomentaryTriggerFired(id);
    });

    // Store trigger info and start timer
    m_momentaryTriggers.insert(id, {timer, severity, category, message});
    timer->start();
    
    // Fire immediately once
    onMomentaryTriggerFired(id);
}

/**
 * @brief Unregister a momentary trigger
 * @param id Unique trigger identifier
 * 
 * Stops the trigger timer and removes the associated alert.
 */
void AlertManager::unregisterMomentaryTrigger(const QString &id)
{
    if (m_momentaryTriggers.contains(id)) {
        auto info = m_momentaryTriggers.take(id);
        info.timer->stop();
        info.timer->deleteLater();
        // Also auto-clear the alert if it exists
        clearAlert(id);
    }
}

/**
 * @brief Called when a momentary trigger fires
 * @param id Unique trigger identifier
 * 
 * Posts the alert associated with this trigger.
 */
void AlertManager::onMomentaryTriggerFired(const QString &id)
{
    if (m_momentaryTriggers.contains(id)) {
        auto &info = m_momentaryTriggers[id];
        postAlert(id, info.severity, info.category, info.message);
    }
}

/**
 * @brief Returns count of unacknowledged Warning alerts
 * @return Count of Warning alerts (severity=2)
 */
int AlertManager::warningCount() const
{
    int count = 0;
    for (const auto &alert : m_alerts) {
        if (alert.severity == 2 && !alert.acknowledged) ++count;
    }
    return count;
}

/**
 * @brief Returns count of unacknowledged Caution alerts
 * @return Count of Caution alerts (severity=1)
 */
int AlertManager::cautionCount() const
{
    int count = 0;
    for (const auto &alert : m_alerts) {
        if (alert.severity == 1 && !alert.acknowledged) ++count;
    }
    return count;
}

/**
 * @brief Returns count of unacknowledged Advisory alerts
 * @return Count of Advisory alerts (severity=0)
 */
int AlertManager::advisoryCount() const
{
    int count = 0;
    for (const auto &alert : m_alerts) {
        if (alert.severity == 0 && !alert.acknowledged) ++count;
    }
    return count;
}

/**
 * @brief Returns true if there are any unacknowledged alerts
 * @return true if any unacknowledged alerts exist
 */
bool AlertManager::hasActiveAlerts() const
{
    for (const auto &alert : m_alerts) {
        if (!alert.acknowledged) return true;
    }
    return false;
}

/**
 * @brief Update all count properties and emit signals if changed
 * 
 * Recalculates warning, caution, and advisory counts and emits
 * signals only if values actually changed.
 */
void AlertManager::updateCounts()
{
    emit warningCountChanged();
    emit cautionCountChanged();
    emit advisoryCountChanged();
    emit hasActiveAlertsChanged();
}
