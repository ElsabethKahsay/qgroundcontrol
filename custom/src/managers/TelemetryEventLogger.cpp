#include "TelemetryEventLogger.h"
#include "TelemetryBridge.h"
#include "ArmingGate.h"
#include "ChecklistEngine.h"
#include "FlightSession.h"
#include "DatabaseManager.h"

TelemetryEventLogger::TelemetryEventLogger(QObject *parent)
    : QObject(parent)
{
}

void TelemetryEventLogger::setDependencies(TelemetryBridge *bridge, ArmingGate *gate, ChecklistEngine *engine)
{
    if (m_bridge == bridge && m_gate == gate && m_engine == engine)
        return;

    if (m_bridge) {
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    if (m_gate) {
        disconnect(m_gate, nullptr, this, nullptr);
    }
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }

    m_bridge = bridge;
    m_gate = gate;
    m_engine = engine;

    if (m_gate) {
        connect(m_gate, &ArmingGate::vehicleArmed, this, [this]() {
            m_maxAltitude = 0.0;
            m_minBatteryV = 999.0;
            m_maxBatteryV = 0.0;
            m_modeChanges = 0;
            m_batteryWarnFired = false;
            m_batteryCritFired = false;
        });

        connect(m_gate, &ArmingGate::vehicleDisarmed, this, [this]() {
            FlightSession *fs = FlightSession::instance();
            if (fs) {
                fs->onDisarmedWithStats(m_maxAltitude, m_minBatteryV, m_maxBatteryV, m_modeChanges);
            }
        });
    }

    if (m_bridge) {
        connect(m_bridge, &TelemetryBridge::flightModeChanged, this, [this]() {
            m_modeChanges++;
        });

        connect(m_bridge, &TelemetryBridge::batteryVoltageChanged, this, [this]() {
            double v = m_bridge->batteryVoltage();
            if (v <= 0.0) return;
            m_maxBatteryV = qMax(m_maxBatteryV, v);
            m_minBatteryV = qMin(m_minBatteryV, v);

            if (!m_batteryWarnFired && v < 21.0) {
                m_batteryWarnFired = true;
                _writeEvent(QStringLiteral("BATTERY_WARN"), QString());
            }
            if (!m_batteryCritFired && v < 19.8) {
                m_batteryCritFired = true;
                _writeEvent(QStringLiteral("BATTERY_CRIT"), QString());
            }
        });
    }
}

void TelemetryEventLogger::_writeEvent(const QString &eventType, const QString &triggeredBy)
{
    FlightSession *fs = FlightSession::instance();
    if (!fs || fs->currentFlightId() <= 0)
        return;

    double batteryV = m_bridge ? m_bridge->batteryVoltage() : 0.0;
    double altitudeM = m_bridge ? m_bridge->altitudeRelative() : 0.0;
    int gpsSats = m_bridge ? m_bridge->gpsSatellites() : 0;
    QString flightMode = m_bridge ? m_bridge->flightMode() : QString();

    DatabaseManager::instance().insertTelemetryEvent(
        fs->currentFlightId(), eventType, triggeredBy,
        batteryV, altitudeM, gpsSats, flightMode);
}
