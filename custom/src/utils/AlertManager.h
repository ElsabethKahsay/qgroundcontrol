// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/AlertManager.h
// Description: Alert severity system with QAbstractListModel for UI binding.
//
// Thread affinity: AlertManager is a Meyers' singleton accessed from the main
// thread only.  All methods are called from QML or signal handlers that
// execute on the GUI thread.  Do NOT call from background threads without
// external synchronization.

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QTimer>
#include <QMap>

struct AlertItem {
    QString id;
    int severity;       // 0: Advisory, 1: Caution, 2: Warning
    QString category;
    QString message;
    QDateTime timestamp;
    bool acknowledged = false;
};

class AlertManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int warningCount READ warningCount NOTIFY warningCountChanged)
    Q_PROPERTY(int cautionCount READ cautionCount NOTIFY cautionCountChanged)
    Q_PROPERTY(int advisoryCount READ advisoryCount NOTIFY advisoryCountChanged)
    Q_PROPERTY(bool hasActiveAlerts READ hasActiveAlerts NOTIFY hasActiveAlertsChanged)

public:
    enum AlertRoles {
        IdRole = Qt::UserRole + 1,
        SeverityRole,
        CategoryRole,
        MessageRole,
        TimestampRole,
        AcknowledgedRole
    };
    Q_ENUM(AlertRoles)

    /**
     * @brief Get the singleton instance of AlertManager
     */
    static AlertManager &instance();

    // ── QAbstractListModel overrides ─────────────────────────────────
    /**
     * @brief Returns the number of alerts in the model
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Returns data for a given role and index
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Returns the role name mapping for QML
     */
    QHash<int, QByteArray> roleNames() const override;

    // ── Alert management ────────────────────────────────────────────────
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
    Q_INVOKABLE void postAlert(const QString &id, int severity, const QString &category, const QString &message);
    /**
     * @brief Mark an alert as acknowledged
     * @param id Unique alert identifier
     * 
     * Acknowledged alerts remain in the list but are not counted as "active".
     */
    Q_INVOKABLE void acknowledgeAlert(const QString &id);
    /**
     * @brief Remove an alert from the list
     * @param id Unique alert identifier
     */
    Q_INVOKABLE void clearAlert(const QString &id);
    /**
     * @brief Remove all alerts from the list
     */
    Q_INVOKABLE void clearAllAlerts();

    // ── Momentary triggers ─────────────────────────────────────────────
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
    Q_INVOKABLE void registerMomentaryTrigger(const QString &id, int intervalMs, int severity, const QString &category, const QString &message);
    /**
     * @brief Unregister a momentary trigger
     * @param id Unique trigger identifier
     * 
     * Stops the trigger timer and removes the associated alert.
     */
    Q_INVOKABLE void unregisterMomentaryTrigger(const QString &id);

    // ── Property getters ────────────────────────────────────────────────
    /**
     * @brief Returns count of unacknowledged Warning alerts
     */
    int warningCount() const;
    /**
     * @brief Returns count of unacknowledged Caution alerts
     */
    int cautionCount() const;
    /**
     * @brief Returns count of unacknowledged Advisory alerts
     */
    int advisoryCount() const;
    /**
     * @brief Returns true if there are any unacknowledged alerts
     */
    bool hasActiveAlerts() const;

signals:
    /**
     * @brief Emitted when warning count changes
     */
    void warningCountChanged();
    /**
     * @brief Emitted when caution count changes
     */
    void cautionCountChanged();
    /**
     * @brief Emitted when advisory count changes
     */
    void advisoryCountChanged();
    /**
     * @brief Emitted when hasActiveAlerts status changes
     */
    void hasActiveAlertsChanged();
    /**
     * @brief Emitted when a new alert is posted
     */
    void newAlertPosted(const QString &id, int severity, const QString &message);

private slots:
    /**
     * @brief Called when a momentary trigger fires
     * @param id Unique trigger identifier
     * 
     * Posts the alert associated with this trigger.
     */
    void onMomentaryTriggerFired(const QString &id);

private:
    /**
     * @brief Constructor for AlertManager
     * @param parent Parent QObject
     */
    explicit AlertManager(QObject *parent = nullptr);
    /**
     * @brief Destructor for AlertManager
     */
    ~AlertManager() override;

    /**
     * @brief Update all count properties and emit signals if changed
     * 
     * Recalculates warning, caution, and advisory counts and emits
     * signals only if values actually changed.
     */
    void updateCounts();

    /**
     * @brief Structure for momentary trigger information
     */
    struct TriggerInfo {
        QTimer *timer;           ///< Timer that fires periodically
        int severity;            ///< Severity level for alerts
        QString category;        ///< Alert category
        QString message;         ///< Alert message
    };

    QList<AlertItem> m_alerts;                       ///< List of all alerts
    QMap<QString, TriggerInfo> m_momentaryTriggers; ///< Active momentary triggers
};
