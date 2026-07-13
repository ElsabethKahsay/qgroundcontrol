#pragma once
#include <QObject>
#include <QHash>
#include <QVector>
#include <QString>
#include <QMetaObject>

class TelemetryBridge;
class ChecklistItemModel;

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
    void _onTelemetryPropertyChanged();

private:
    void _rebuildBindings();
    void _evaluateItem(int row);
    double _readBridgeProperty(const QString &prop) const;
    static QString _signalNameForProperty(const QString &prop);

    TelemetryBridge *m_bridge = nullptr;
    ChecklistItemModel *m_model = nullptr;
    bool m_running = false;

    QHash<QString, QVector<int>> m_bindings;
    QVector<QMetaObject::Connection> m_connections;
};
