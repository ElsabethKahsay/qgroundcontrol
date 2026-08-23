#include "TelemetryEventLogger.h"
#include "TelemetryBridge.h"
#include "ArmingGate.h"
#include "ChecklistEngine.h"
#include "FlightSession.h"
#include "DatabaseManager.h"

#include <QtMath>

namespace {
// Haversine great-circle distance between two lat/lon points (degrees → meters).
double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371000.0; // Earth radius in meters
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2.0) * qSin(dLat / 2.0)
                   + qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2))
                     * qSin(dLon / 2.0) * qSin(dLon / 2.0);
    return r * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
}
}

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
            m_maxGroundSpeedMs = 0.0;
            m_maxVerticalSpeedMs = 0.0;
            m_distanceFlownM = 0.0;
            m_batterySumV = 0.0;
            m_batterySamples = 0;
            m_batteryWarnFired = false;
            m_batteryCritFired = false;
            m_hasPrevPosition = false;
        });

        connect(m_gate, &ArmingGate::vehicleDisarmed, this, [this]() {
            FlightSession *fs = FlightSession::instance();
            if (fs) {
                double avgBatteryV = (m_batterySamples > 0) ? (m_batterySumV / m_batterySamples) : 0.0;
                fs->onDisarmedWithStats(m_maxAltitude, m_minBatteryV, m_maxBatteryV, m_modeChanges,
                                        m_maxGroundSpeedMs, m_maxVerticalSpeedMs,
                                        m_distanceFlownM, avgBatteryV);
            }
        });
    }

    if (m_bridge) {
        connect(m_bridge, &TelemetryBridge::flightModeChanged, this, [this]() {
            m_modeChanges++;
        });

        connect(m_bridge, &TelemetryBridge::groundSpeedChanged, this, [this]() {
            double gs = m_bridge->groundSpeed();
            if (gs > 0.0)
                m_maxGroundSpeedMs = qMax(m_maxGroundSpeedMs, gs);
        });

        connect(m_bridge, &TelemetryBridge::verticalSpeedChanged, this, [this]() {
            double vs = qAbs(m_bridge->verticalSpeed());
            if (vs > 0.0)
                m_maxVerticalSpeedMs = qMax(m_maxVerticalSpeedMs, vs);
        });

        connect(m_bridge, &TelemetryBridge::gpsPositionChanged, this, [this]() {
            double lat = m_bridge->gpsLatitude();
            double lon = m_bridge->gpsLongitude();
            if (qAbs(lat) < 0.0001 && qAbs(lon) < 0.0001)
                return; // no valid fix yet
            if (m_hasPrevPosition) {
                m_distanceFlownM += haversineMeters(m_prevLat, m_prevLon, lat, lon);
            }
            m_prevLat = lat;
            m_prevLon = lon;
            m_hasPrevPosition = true;
        });

        connect(m_bridge, &TelemetryBridge::batteryVoltageChanged, this, [this]() {
            double v = m_bridge->batteryVoltage();
            if (v <= 0.0) return;
            m_maxBatteryV = qMax(m_maxBatteryV, v);
            m_minBatteryV = qMin(m_minBatteryV, v);
            m_batterySumV += v;
            m_batterySamples++;

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
