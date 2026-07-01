#pragma once

#include <QObject>
#include <QVariant>
#include <QString>

class ParameterManager;
class Vehicle;

class QgcParameterAdapter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
public:
    explicit QgcParameterAdapter(ParameterManager* paramMgr, QObject* parent = nullptr);
    ~QgcParameterAdapter() override;

    Q_INVOKABLE QVariant getParam(const QString& name);
    Q_INVOKABLE bool setParam(const QString& name, const QVariant& value);
    Q_INVOKABLE bool isReady() const;
    Q_INVOKABLE QString autopilotType();

signals:
    void readyChanged();
    void parameterChanged(const QString& name, const QVariant& value);
    void fallbackActivated();

private slots:
    void _onLoadProgressChanged(float progress);

private:
    ParameterManager* _paramMgr;
    bool _ready;
    QString _autopilot;
};
