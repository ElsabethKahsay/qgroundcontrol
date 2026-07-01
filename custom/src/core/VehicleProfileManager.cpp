#include "VehicleProfileManager.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "Vehicle.h"

#include "DatabaseManager.h"
#include "TelemetryBridge.h"

VehicleProfileManager::VehicleProfileManager(QObject *parent)
    : QObject(parent)
{
}

void VehicleProfileManager::setTelemetryBridge(TelemetryBridge *bridge)
{
    if (m_telemetry == bridge) return;
    if (m_telemetry) {
        disconnect(m_telemetry, nullptr, this, nullptr);
    }
    m_telemetry = bridge;
    if (m_telemetry) {
        connect(m_telemetry, &TelemetryBridge::isConnectedChanged, this, &VehicleProfileManager::onConnectionChanged);
    }
}

void VehicleProfileManager::onConnectionChanged()
{
    if (!m_telemetry || !m_telemetry->isConnected()) {
        // End flight session
        if (m_flightSessionId > 0) {
            double sec = m_sessionTimer.elapsed() / 1000.0;
            DatabaseManager::instance().endFlightSession(m_flightSessionId, sec);
            if (!m_currentDeviceUid.isEmpty()) {
                DatabaseManager::instance().updateFlightHours(m_currentDeviceUid, sec / 3600.0);
            }
            m_flightSessionId = -1;
        }
        m_currentDeviceUid.clear();
        m_currentVehicleHistoryJson.clear();
        emit currentVehicleChanged();
        return;
    }

    // Connected: resolve device UID
    m_currentDeviceUid = resolveDeviceUid();
    if (m_currentDeviceUid.isEmpty()) {
        qWarning() << "VehicleProfileManager: could not resolve device UID";
        return;
    }

    QString apType = autopilotTypeString();
    QString afType = airframeTypeString();

    // Upsert vehicle profile
    DatabaseManager::instance().upsertVehicle(m_currentDeviceUid, {}, apType, afType);

    // Load full history
    m_currentVehicleHistoryJson = DatabaseManager::instance().getVehicleHistory(m_currentDeviceUid);
    if (m_currentVehicleHistoryJson.isEmpty()) {
        // Minimal JSON if no db record
        QJsonObject o;
        o["deviceUid"] = m_currentDeviceUid;
        o["autopilotType"] = apType;
        o["airframeType"] = afType;
        m_currentVehicleHistoryJson = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

    // Start flight session with current payload weight
    m_flightSessionId = DatabaseManager::instance().startFlightSession(m_currentDeviceUid, m_batterySerial, m_payloadWeightKg);
    m_sessionTimer.start();

    emit currentVehicleChanged();
}

QString VehicleProfileManager::resolveDeviceUid()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};

    // Primary: hardware UID from AUTOPILOT_VERSION.uid
    quint64 uid = v->vehicleUID();
    if (uid != 0) {
        return v->vehicleUIDStr();
    }

    // Fallback: composite of autopilot type + airframe type
    QString composite = QStringLiteral("composite:%1:%2")
        .arg(autopilotTypeString(), airframeTypeString());
    if (!composite.isEmpty() && composite != "composite::") {
        return composite;
    }

    // Last resort: MAV_SYS_ID (session-only, clearly non-ideal)
    return QStringLiteral("sysid:%1").arg(m_telemetry->vehicle()->id());
}

QString VehicleProfileManager::autopilotTypeString()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};
    if (v->px4Firmware()) return QStringLiteral("PX4");
    if (v->apmFirmware()) return QStringLiteral("ArduPilot");
    return QStringLiteral("Generic");
}

QString VehicleProfileManager::airframeTypeString()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};
    if (v->multiRotor()) return QStringLiteral("MultiRotor");
    if (v->fixedWing()) return QStringLiteral("FixedWing");
    if (v->vtol()) return QStringLiteral("VTOL");
    if (v->rover()) return QStringLiteral("Rover");
    if (v->sub()) return QStringLiteral("Sub");
    if (v->airship()) return QStringLiteral("Airship");
    return QStringLiteral("Unknown");
}

void VehicleProfileManager::setBatterySerial(const QString &serial, const QString &operatorLabel)
{
    m_batterySerial = serial;
    if (!serial.isEmpty()) {
        DatabaseManager::instance().upsertBattery(serial, operatorLabel);
    }
    emit currentBatteryChanged();
}

QString VehicleProfileManager::loadVehicleHistory(const QString &deviceUid)
{
    return DatabaseManager::instance().getVehicleHistory(deviceUid);
}

QStringList VehicleProfileManager::knownVehicles()
{
    return DatabaseManager::instance().listVehicles();
}

void VehicleProfileManager::setPayloadWeightKg(double kg)
{
    if (qFuzzyCompare(m_payloadWeightKg, kg)) return;
    m_payloadWeightKg = qMax(0.0, kg);
    // Persist to current flight session if active
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionPayload(m_flightSessionId, m_payloadWeightKg);
    }
    emit payloadWeightChanged();
}


