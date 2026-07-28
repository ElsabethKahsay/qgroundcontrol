#pragma once

// ============================================================================
// MaintenanceTracker — Tracks runtime usage of replaceable hardware
// components (motors, props, batteries, ESCs, etc.) and emits warnings
// when they approach their service-life limits.
//
// Components are stored in DatabaseManager and tracked in two dimensions:
//   - Hours: accumulated while the vehicle is armed
//   - Cycles: incremented each time the vehicle disarms after a flight
//
// When armed, a 60-second timer periodically checks thresholds.
// On disarm, the elapsed armed time is added to all components and
// their cycle counts are incremented.
//
// Emits maintenanceWarning / maintenanceCritical signals when a
// component reaches configurable percentage thresholds (defined in
// Config.h as kMaintWarningPercent / kMaintCriticalPercent).
// ============================================================================

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>

class MaintenanceTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)

public:
    explicit MaintenanceTracker(QObject *parent = nullptr);

    /// Current list of tracked components as QVariantMap list (for QML binding).
    QVariantList components() const { return m_components; }

    /// True while the vehicle is armed and elapsed-time tracking is active.
    bool armed() const { return m_armed; }

    /// Register a new hardware component to track (persisted via DatabaseManager).
    Q_INVOKABLE void addComponent(const QString &name, const QString &type,
                                  double maxHours = 0, int maxCycles = 0);

    /// Remove a tracked component by its database ID.
    Q_INVOKABLE void removeComponent(int id);

    /// Reset a component's hour/cycle counters to zero (after replacement).
    Q_INVOKABLE void resetComponent(int id, const QString &notes = QString());

    /// Reload the component list from DatabaseManager and emit componentsChanged.
    Q_INVOKABLE void refreshComponents();

    /// Update armed state. On arm, starts the elapsed-time timer; on disarm,
    /// distributes the armed time to all components and increments their cycles.
    Q_INVOKABLE void setArmed(bool armed);

signals:
    void componentsChanged();
    void armedChanged();

    /// Emitted when a component reaches the warning threshold percentage.
    void maintenanceWarning(int componentId, const QString &name, int percent);

    /// Emitted when a component reaches the critical threshold percentage.
    void maintenanceCritical(int componentId, const QString &name, int percent);

private slots:
    /// Periodic callback while armed; checks all components against thresholds.
    void onTick();

private:
    /// Compare each component's usage against warning/critical thresholds and emit signals.
    void checkThresholds();

    QVariantList m_components;
    bool m_armed = false;
    QElapsedTimer m_armedTimer;
    QTimer m_tickTimer;
};
