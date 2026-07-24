/**
 * @file ChecklistEngine.h
 * @brief Signal-driven engine that evaluates checklist items by binding to TelemetryBridge properties.
 *
 * Connects to TelemetryBridge property change signals and re-evaluates only the
 * checklist items that depend on the changed property. Each ChecklistItemData has
 * a bindProperty name (e.g. "batteryVoltage") and a tolerance range; the engine
 * reads the current telemetry value and compares it to the required value ± tolerance.
 *
 * Works with ChecklistItemModel (the QAbstractListModel for QML display).
 */

#pragma once
#include <QObject>
#include <QHash>
#include <QVector>
#include <QString>
#include <QMetaObject>

class TelemetryBridge;
class ChecklistItemModel;

/**
 * Evaluates checklist items against live telemetry data.
 *
 * Usage:
 *   engine->setTelemetryBridge(bridge);
 *   engine->setModel(checklistModel);
 *   engine->start();  // begins reactive evaluation
 */
class ChecklistEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(int totalItems READ totalItems NOTIFY totalItemsChanged)
    Q_PROPERTY(int passedItems READ passedItems NOTIFY evaluationCompleted)
    Q_PROPERTY(int pendingItems READ pendingItems NOTIFY evaluationCompleted)
    Q_PROPERTY(int failedItems READ failedItems NOTIFY evaluationCompleted)
    Q_PROPERTY(bool allPassed READ allPassed NOTIFY evaluationCompleted)
public:
    explicit ChecklistEngine(QObject *parent = nullptr);

    void setTelemetryBridge(TelemetryBridge *bridge);
    TelemetryBridge *telemetryBridge() const { return m_bridge; }

    void setModel(ChecklistItemModel *model);
    ChecklistItemModel *model() const { return m_model; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void evaluateAll();
    Q_INVOKABLE void confirmItem(int row, const QString &operatorId = {});

    int totalItems() const;
    int passedItems() const;
    int pendingItems() const;
    int failedItems() const;
    bool allPassed() const;

signals:
    void totalItemsChanged();
    void evaluationCompleted();
    void itemConfirmed(int row, const QString &operatorId);

private slots:
    /// Called when a connected TelemetryBridge property changes; re-evaluates affected items.
    void _onTelemetryPropertyChanged();

private:
    /// Disconnect old signal connections and rebuild from current model bindings.
    void _rebuildBindings();
    /// Evaluate a single checklist item by reading its bound telemetry property.
    void _evaluateItem(int row);
    /// Read a double property from the TelemetryBridge (NaN if unavailable).
    double _readBridgeProperty(const QString &prop) const;
    /// Convert a property name to its Qt signal name (e.g. "batteryVoltage" → "batteryVoltageChanged").
    static QString _signalNameForProperty(const QString &prop);

    TelemetryBridge *m_bridge = nullptr;
    ChecklistItemModel *m_model = nullptr;
    bool m_running = false;

    /// Reverse mapping: telemetry property name → model rows that depend on it
    QHash<QString, QVector<int>> m_bindings;
    /// Active signal connections to TelemetryBridge (cleaned up on rebuild)
    QVector<QMetaObject::Connection> m_connections;
};
