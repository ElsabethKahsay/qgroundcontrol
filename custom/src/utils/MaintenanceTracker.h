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

    QVariantList components() const { return m_components; }
    bool armed() const { return m_armed; }

    Q_INVOKABLE void addComponent(const QString &name, const QString &type,
                                  double maxHours = 0, int maxCycles = 0);
    Q_INVOKABLE void removeComponent(int id);
    Q_INVOKABLE void resetComponent(int id, const QString &notes = QString());
    Q_INVOKABLE void refreshComponents();
    Q_INVOKABLE void setArmed(bool armed);

signals:
    void componentsChanged();
    void armedChanged();
    void maintenanceWarning(int componentId, const QString &name, int percent);
    void maintenanceCritical(int componentId, const QString &name, int percent);

private slots:
    void onTick();

private:
    void checkThresholds();

    QVariantList m_components;
    bool m_armed = false;
    QElapsedTimer m_armedTimer;
    QTimer m_tickTimer;
};
