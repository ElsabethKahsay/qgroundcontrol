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

/// Swap the telemetry bridge, reconnecting signals for connection and arm state.
void VehicleProfileManager::setTelemetryBridge(TelemetryBridge *bridge)
{
    if (m_telemetry == bridge) return;
    if (m_telemetry) {
        disconnect(m_telemetry, nullptr, this, nullptr);
    }
    m_telemetry = bridge;
    if (m_telemetry) {
        connect(m_telemetry, &TelemetryBridge::isConnectedChanged, this, &VehicleProfileManager::onConnectionChanged);
        connect(m_telemetry, &TelemetryBridge::armedChanged, this, [this]() {
            _onArmedChanged(m_telemetry->armed());
        });
    }
}

/// Called when the telemetry link connects or disconnects.
/// On disconnect: ends the flight session, accumulates flight hours, saves last GPS.
/// On connect: resolves device UID, upserts profile in DB, starts a new flight session.
void VehicleProfileManager::onConnectionChanged()
{
    if (!m_telemetry || !m_telemetry->isConnected()) {
        // End flight session — auto-save expanded profile
        if (m_flightSessionId > 0) {
            double sec = m_sessionTimer.elapsed() / 1000.0;
            DatabaseManager::instance().endFlightSession(m_flightSessionId, sec);
            if (!m_currentDeviceUid.isEmpty()) {
                DatabaseManager::instance().updateFlightHours(m_currentDeviceUid, sec / 3600.0);
                DatabaseManager::instance().incrementFlightCount(m_currentDeviceUid);
            }
        }
        // Auto-save the vehicle's last known GPS position on disconnect.
        if (!m_currentDeviceUid.isEmpty()) {
            double lat = m_telemetry ? m_telemetry->gpsLatitude() : 0.0;
            double lon = m_telemetry ? m_telemetry->gpsLongitude() : 0.0;
            if (qAbs(lat) > 0.1 || qAbs(lon) > 0.1)
                DatabaseManager::instance().updateVehicleGps(m_currentDeviceUid, lat, lon);
        }
        m_flightSessionId = -1;
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

    // Read airframe/frame parameters to expand the stored profile with hardware details.
    int frameClass = -1, frameType = -1, motorCount = 0;
    double gpsLat = 0.0, gpsLon = 0.0;
    if (m_telemetry) {
        gpsLat = m_telemetry->gpsLatitude();
        gpsLon = m_telemetry->gpsLongitude();
        if (m_telemetry->hasParameter(QStringLiteral("FRAME_CLASS")))
            frameClass = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("FRAME_CLASS"), -1.0f));
        if (m_telemetry->hasParameter(QStringLiteral("FRAME_TYPE")))
            frameType = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("FRAME_TYPE"), -1.0f));
        // PX4 uses CA_AIRFRAME where ArduPilot uses FRAME_CLASS — both map to frameClass here.
        if (m_telemetry->hasParameter(QStringLiteral("CA_AIRFRAME")))
            frameClass = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("CA_AIRFRAME"), -1.0f));
        motorCount = m_telemetry->motorCount();
    }

    // Upsert vehicle profile with expanded data
    DatabaseManager::instance().upsertVehicleEx(m_currentDeviceUid, {}, apType, afType,
                                                 frameClass, frameType, motorCount, {},
                                                 gpsLat, gpsLon);

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

/// Resolve a persistent device identifier for the connected vehicle.
/// Priority: hardware UID (stable across sessions) > composite key > system ID (ephemeral).
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

    // Fallback: composite of autopilot type + airframe type — not unique per board but stable.
    QString composite = QStringLiteral("composite:%1:%2")
        .arg(autopilotTypeString(), airframeTypeString());
    if (!composite.isEmpty() && composite != "composite::") {
        return composite;
    }

    // Last resort: MAV_SYS_ID (session-only, clearly non-ideal)
    return QStringLiteral("sysid:%1").arg(m_telemetry->vehicle()->id());
}

int VehicleProfileManager::resolveMotorCount(TelemetryBridge *telemetry, int defaultCount)
{
    if (!telemetry) {
        qWarning() << "VehicleProfileManager::resolveMotorCount: no telemetry, using default" << defaultCount;
        return defaultCount;
    }

    Vehicle *v = telemetry->vehicle();
    if (!v) {
        qWarning() << "VehicleProfileManager::resolveMotorCount: no vehicle, using default" << defaultCount;
        return defaultCount;
    }

    if (v->px4Firmware() && telemetry->hasParameter("CA_AIRFRAME")) {
        int frame = static_cast<int>(telemetry->parameterValue("CA_AIRFRAME", 1.0f));
        switch (frame) {
        case 0:  return 4;  // Generic
        case 1:  return 4;  // Quadrotor X
        case 2:  return 6;  // Hexarotor X
        case 3:  return 8;  // Octorotor X
        case 4:  return 8;  // Octorotor Plus
        case 5:  return 3;  // Tricopter Y
        case 6:  return 4;  // Coaxial
        case 7:  return 6;  // TiltHex
        case 8:  return 1;  // Helicopter
        case 9:  return defaultCount; // Custom
        case 10: return 4;  // Quadrotor +
        case 11: return 6;  // Hexarotor +
        case 12: return 8;  // Octorotor X (DJA)
        case 13: return 12; // Dodecarotor
        case 14: return 8;  // Simul
        default:
            qWarning() << "VehicleProfileManager: unknown CA_AIRFRAME" << frame << "using default" << defaultCount;
            return defaultCount;
        }
    }

    if (v->apmFirmware() && telemetry->hasParameter("FRAME_CLASS")) {
        int frameClass = static_cast<int>(telemetry->parameterValue("FRAME_CLASS", 0.0f));
        if (frameClass == 0) { // MultiRotor
            if (telemetry->hasParameter("FRAME_TYPE")) {
                int frameType = static_cast<int>(telemetry->parameterValue("FRAME_TYPE", 10.0f));
                switch (frameType) {
                case 10: return 4;  // Quad
                case 11: return 6;  // Hexa
                case 12: return 8;  // Octa
                default: return 4;  // Generic multirotor
                }
            }
        }
        qWarning() << "VehicleProfileManager: unsupported ArduPilot FRAME_CLASS" << frameClass
                    << "using default" << defaultCount;
        return defaultCount;
    }

    qWarning() << "VehicleProfileManager::resolveMotorCount: unknown firmware, using default" << defaultCount;
    return defaultCount;
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

/// Determine airframe type string from firmware parameters (most accurate),
/// falling back to HEARTBEAT-based vehicle type detection.
QString VehicleProfileManager::airframeTypeString()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};

    // Try parameter-based detection first (more accurate)
    QString apType = autopilotTypeString();
    if (apType == QStringLiteral("ArduPilot") && m_telemetry->hasParameter(QStringLiteral("FRAME_CLASS"))) {
        int fc = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("FRAME_CLASS"), -1.0f));
        switch (fc) {
        case 0: return QStringLiteral("Plane");
        case 1: return QStringLiteral("Quad");
        case 2: return QStringLiteral("Hexa");
        case 3: return QStringLiteral("Octa");
        case 4: return QStringLiteral("OctaQuad");
        case 5: return QStringLiteral("Y6");
        case 6: return QStringLiteral("Heli");
        case 7: return QStringLiteral("Tri");
        default: break;
        }
    }
    if (apType == QStringLiteral("PX4") && m_telemetry->hasParameter(QStringLiteral("CA_AIRFRAME"))) {
        int af = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("CA_AIRFRAME"), -1.0f));
        switch (af) {
        case 0: return QStringLiteral("Multicopter");
        case 1: return QStringLiteral("Plane");
        case 2: return QStringLiteral("FlyingWing");
        case 3: return QStringLiteral("Rover");
        case 4: return QStringLiteral("VTOL Tiltrotor");
        case 5: return QStringLiteral("VTOL Standard");
        case 6: return QStringLiteral("Tailsitter");
        case 7: return QStringLiteral("Boat");
        case 8: return QStringLiteral("Quad");
        case 9: return QStringLiteral("Hexa");
        case 10: return QStringLiteral("Octo");
        default: break;
        }
    }

    // Fallback to HEARTBEAT-based detection
    if (v->multiRotor()) return QStringLiteral("MultiRotor");
    if (v->fixedWing()) return QStringLiteral("FixedWing");
    if (v->vtol()) return QStringLiteral("VTOL");
    if (v->rover()) return QStringLiteral("Rover");
    if (v->sub()) return QStringLiteral("Sub");
    if (v->airship()) return QStringLiteral("Airship");
    return QStringLiteral("Unknown");
}

/// Set the active battery serial number and register/upsert it in the battery database.
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

bool VehicleProfileManager::deleteVehicle(const QString &deviceUid)
{
    return DatabaseManager::instance().deleteVehicle(deviceUid);
}

bool VehicleProfileManager::incrementFlightCount(const QString &deviceUid)
{
    return DatabaseManager::instance().incrementFlightCount(deviceUid);
}

bool VehicleProfileManager::updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                                   const QString &notes)
{
    return DatabaseManager::instance().updateVehicleProfile(deviceUid, pilotName, notes);
}

bool VehicleProfileManager::updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion)
{
    return DatabaseManager::instance().updateVehicleFirmware(fingerprint, firmwareVersion);
}

void VehicleProfileManager::setPayloadWeightKg(double kg)
{
    if (qFuzzyCompare(m_payloadWeightKg, kg)) return;
    m_payloadWeightKg = qMax(0.0, kg);
    // Persist to current flight session if one is active.
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionPayload(m_flightSessionId, m_payloadWeightKg);
    }
    emit payloadWeightChanged();
}

void VehicleProfileManager::setLocationName(const QString &name)
{
    if (m_locationName == name) return;
    m_locationName = name;
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionLocation(m_flightSessionId, m_locationName);
    }
    emit locationNameChanged();
}

void VehicleProfileManager::setPlanLatitude(double lat)
{
    if (qFuzzyCompare(m_planLat, lat)) return;
    m_planLat = lat;
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionPlanLocation(m_flightSessionId, m_planLat, m_planLon);
    }
    emit planLatitudeChanged();
}

void VehicleProfileManager::setPlanLongitude(double lon)
{
    if (qFuzzyCompare(m_planLon, lon)) return;
    m_planLon = lon;
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionPlanLocation(m_flightSessionId, m_planLat, m_planLon);
    }
    emit planLongitudeChanged();
}

/// Track arm/disarm transitions.
/// On arm: start the armed timer and record battery % for delta calculation.
/// On disarm: if armed long enough (configurable threshold, default 30 s), record
/// a battery cycle, estimate energy consumption, and log flight distance.
void VehicleProfileManager::_onArmedChanged(bool armed)
{
    if (armed == m_wasArmed)
        return;
    m_wasArmed = armed;

    if (armed) {
        m_armedTimer.start();
        if (m_telemetry)
            m_armBatteryPct = m_telemetry->property("batteryPercent").toDouble();
    } else {
        // Disarm — check if it was a real flight (not just a test arm)
        int armedThresholdMs = [this]() {
        QString val = DatabaseManager::instance().getCheckConfig("vehicle_profile", "armed_timer_threshold_ms");
        if (!val.isEmpty()) { bool ok; int v = val.toInt(&ok); if (ok) return v; }
        return 30000;
    }();
    if (m_armedTimer.isValid() && m_armedTimer.elapsed() > armedThresholdMs) {
            // Record a battery cycle (discharge event) for battery health tracking.
            if (!m_batterySerial.isEmpty() && m_flightSessionId > 0) {
                double capacityAtFull = m_telemetry
                    ? m_telemetry->property("batteryPercent").toDouble() / 100.0
                    : 0.0;
                DatabaseManager::instance().saveBatteryCycle(
                    m_batterySerial, m_flightSessionId,
                    capacityAtFull, 0.0, 0.0, 0);
                qDebug().noquote()
                    << QStringLiteral("Battery cycle recorded for %1 (session %2)")
                           .arg(m_batterySerial).arg(m_flightSessionId);
            }

            // Record energy consumption for power model calibration.
            if (m_flightSessionId > 0 && m_telemetry) {
                double disarmPct = m_telemetry->property("batteryPercent").toDouble();
                double pctUsed = 0.0;
                if (m_armBatteryPct > 0 && disarmPct >= 0)
                    pctUsed = qBound(0.0, m_armBatteryPct - disarmPct, 100.0);
                double batteryVoltage = m_telemetry->property("batteryVoltage").toDouble();
                // Use battery capacity parameter if available for accurate Wh calculation;
                // fall back to a rough 0.45 Ah-per-percent estimate otherwise.
                double energyWh = pctUsed * batteryVoltage * 0.45;
                if (m_telemetry->hasParameter("BAT_CAPACITY")) {
                    float capMah = m_telemetry->parameterValue("BAT_CAPACITY", 0.0f);
                    if (capMah > 0)
                        energyWh = (pctUsed / 100.0) * capMah * batteryVoltage / 1000.0;
                } else if (m_telemetry->hasParameter("BAT1_CAPACITY")) {
                    float capMah = m_telemetry->parameterValue("BAT1_CAPACITY", 0.0f);
                    if (capMah > 0)
                        energyWh = (pctUsed / 100.0) * capMah * batteryVoltage / 1000.0;
                }

                double distM = m_telemetry->property("missionTotalDistance").toDouble();
                if (distM <= 0.0) {
                    // Fallback: estimate from ground speed * duration
                    double gndSpd = m_telemetry->property("groundSpeed").toDouble();
                    double durSec = m_armedTimer.elapsed() / 1000.0;
                    distM = gndSpd * durSec;
                }
                DatabaseManager::instance().updateFlightSessionEnergy(
                    m_flightSessionId, energyWh, distM);
            }
        }
        m_armBatteryPct = -1.0;
    }
}


