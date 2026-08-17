#pragma once
#include <QObject>

class TelemetryBridge;
class ArmingGate;
class ChecklistEngine;

class TelemetryEventLogger : public QObject
{
    Q_OBJECT
public:
    explicit TelemetryEventLogger(QObject *parent = nullptr);

    void setDependencies(TelemetryBridge *bridge, ArmingGate *gate, ChecklistEngine *engine);

private:
    void _writeEvent(const QString &eventType, const QString &triggeredBy);

    TelemetryBridge  *m_bridge  = nullptr;
    ArmingGate       *m_gate    = nullptr;
    ChecklistEngine  *m_engine  = nullptr;

    double  m_maxAltitude  = 0.0;
    double  m_minBatteryV  = 999.0;
    double  m_maxBatteryV  = 0.0;
    int     m_modeChanges  = 0;
    bool    m_batteryWarnFired  = false;
    bool    m_batteryCritFired  = false;
};
