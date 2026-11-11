/**
 * @file VehicleProfileManager.cpp
 * @brief Per-vehicle profile management, flight session lifecycle, and battery tracking.
 *
 * Handles device UID resolution, upserts vehicle profiles to the database on
 * connect, manages flight session start/end, records battery cycles on disarm,
 * and estimates energy consumption for power model calibration.
 */

#include "VehicleProfileManager.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

#include "Vehicle.h"

#include "utils/Config.h"
#include "detection/VehicleRegistry.h"
#include "DatabaseManager.h"
#include "PowerModel.h"
#include "PowerTypeModel.h"
#include "MotorTable.h"
#include "WeatherProvider.h"
#include "controllers/HardwareTestController.h"
#include "TelemetryBridge.h"
#include "MultiVehicleManager.h"
#include "MAVLink/QGCMAVLink.h"
#include "QmlObjectListModel.h"
#include "Vehicle/FactGroups/VehicleBatteryFactGroup.h"
#include "MissionWalk.h"
#include "MissionAssess.h"
#include "MissionManager/MissionManager.h"
#include "MissionManager/MissionItem.h"
#include "managers/FlightSession.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"
#include "utils/PreflightSettingsManager.h"

// The copter's parameters as MissionWalk reads them: true and the value when
// the bridge has read it from the copter.
static MissionWalk::Lookup copterParams(const TelemetryBridge *bridge)
{
    return [bridge](const QString &name, double *value) {
        if (!bridge || !bridge->hasParameter(name))
            return false;
        *value = static_cast<double>(bridge->parameterValue(name));
        return true;
    };
}

// The cruise ratio (cruise power ÷ hover power) last measured on this copter
// in the speed band of @p speedMs; -1 when that band has not been flown long
// enough.
static double storedCruiseRatio(const QString &uid, double speedMs)
{
    const int band = MotorTable::cruiseBand(speedMs);
    if (band < 0)
        return -1.0;
    return QSettings().value(QStringLiteral("MotorTable/cruiseBand/%1/%2").arg(uid).arg(band), -1.0).toDouble();
}

VehicleProfileManager *VehicleProfileManager::s_instance = nullptr;

namespace {
// Default thrust→current table (g → A) — HobbyWing-style multirotor propulsion
// set.  Used until a per-vehicle pull-test datasheet is loaded; operators can
// replace it from the battery estimator UI with values derived from their own
// motor data.
const QVector<QPair<double, double>> kDefaultThrustTable = {
    {  500,  3.0 },
    { 1000,  5.5 },
    { 1500,  8.0 },
    { 2000, 10.5 },
    { 2500, 12.8 },
    { 2668, 13.0 },
    { 3000, 16.0 },
    { 3500, 20.0 },
    { 4000, 25.0 },
};
}

VehicleProfileManager *VehicleProfileManager::instance()
{
    return s_instance;
}

VehicleProfileManager::VehicleProfileManager(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    _applyDefaultThrustTable();

    // Live battery estimate refresh (SOC/current → remaining flight time)
    // delivered to the FlyView chip. Runs every 2 s while a vehicle is connected.
    m_liveTimer.setInterval(2000);
    m_liveTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_liveTimer, &QTimer::timeout, this, &VehicleProfileManager::_updateLiveEstimate);
    // A changed type, link, charger number or used/dismissed flight changes
    // the model the live estimate uses: re-fit it on the next update.
    connect(this, &VehicleProfileManager::powerTypeChanged, this, [this]() { m_powerModelDirty = true; });
}

VehicleProfileManager::~VehicleProfileManager()
{
    if (s_instance == this)
        s_instance = nullptr;
}

/// Map a MAVLink MAV_TYPE to the canonical VehicleKind.
VehicleKind VehicleProfileManager::kindFromMavType(int mavType)
{
    switch (mavType) {
    case MAV_TYPE_QUADROTOR:
    case MAV_TYPE_HEXAROTOR:
    case MAV_TYPE_OCTOROTOR:
    case MAV_TYPE_TRICOPTER: return VehicleKind::Multirotor;
    case MAV_TYPE_FIXED_WING:
    case MAV_TYPE_FLAPPING_WING: return VehicleKind::FixedWing;
    case MAV_TYPE_VTOL_FIXEDROTOR:
    case MAV_TYPE_VTOL_RESERVED5: return VehicleKind::VtolConventional;
    default: return VehicleKind::Unknown;
    }
}

/// Map a resolved vehicle-type string to the canonical VehicleKind.
VehicleKind VehicleProfileManager::kindFromTypeString(const QString &type)
{
    QString t = type.toUpper();
    if (t == QStringLiteral("QUAD") || t == QStringLiteral("HEX")
        || t == QStringLiteral("OCTA") || t == QStringLiteral("TRI")
        || t == QStringLiteral("COPTER") || t == QStringLiteral("MULTIROTOR"))
        return VehicleKind::Multirotor;
    if (t == QStringLiteral("FIXED_WING") || t == QStringLiteral("PLANE")
        || t == QStringLiteral("FIXEDWING") || t == QStringLiteral("FIXED"))
        return VehicleKind::FixedWing;
    if (t == QStringLiteral("VTOL") || t == QStringLiteral("VTOL_CONVENTIONAL"))
        return VehicleKind::VtolConventional;
    return VehicleKind::Unknown;
}

QString VehicleProfileManager::kindString(VehicleKind kind)
{
    switch (kind) {
    case VehicleKind::Multirotor: return QStringLiteral("MULTIROTOR");
    case VehicleKind::FixedWing: return QStringLiteral("FIXED_WING");
    case VehicleKind::VtolConventional: return QStringLiteral("VTOL_CONVENTIONAL");
    case VehicleKind::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

/// Swap the telemetry bridge, reconnecting signals for connection and arm state.
void VehicleProfileManager::setArmingGate(QObject *gate)
{
    m_armingGate = gate;
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
        connect(m_telemetry, &TelemetryBridge::armedChanged, this, [this]() {
            _onArmedChanged(m_telemetry->armed());
        });
        connect(m_telemetry, &TelemetryBridge::parametersReadyChanged, this, &VehicleProfileManager::resolveVehicleTypeAndMotorCount);
        connect(m_telemetry, &TelemetryBridge::motorOutputsReceived, this, &VehicleProfileManager::_onMotorOutputs);
        connect(m_telemetry, &TelemetryBridge::motorOutputsReceived, this, &VehicleProfileManager::_onOutputsForDrop);
        connect(m_telemetry, &TelemetryBridge::parameterUpdated, this, [this](const QString &, float) {
            resolveVehicleTypeAndMotorCount();
        });
    }
}

void VehicleProfileManager::resolveVehicleTypeAndMotorCount()
{
    if (!m_telemetry || !m_telemetry->isConnected()) {
        if (m_typeResolved) {
            m_typeResolved = false;
            m_vehicleType = QStringLiteral("UNKNOWN");
            m_kind = VehicleKind::Unknown;
            m_motorCount = 0;
            emit vehicleTypeResolved();
        }
        return;
    }

    Vehicle *v = m_telemetry->vehicle();
    if (!v) return;

    bool resolved = false;
    QString vType = QStringLiteral("UNKNOWN");
    int mCount = 1;

    // ArduPilot priority: FRAME_CLASS
    if (v->apmFirmware() || m_telemetry->hasParameter(QStringLiteral("FRAME_CLASS"))) {
        if (m_telemetry->hasParameter(QStringLiteral("FRAME_CLASS"))) {
            int fc = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("FRAME_CLASS"), -1.0f));
            switch (fc) {
            case 1: vType = QStringLiteral("QUAD");       mCount = 4; resolved = true; break;
            case 2: vType = QStringLiteral("HEX");        mCount = 6; resolved = true; break;
            case 3: vType = QStringLiteral("OCTA");       mCount = 8; resolved = true; break;
            case 4: vType = QStringLiteral("OCTA");       mCount = 8; resolved = true; break;
            case 5: vType = QStringLiteral("HEX");        mCount = 6; resolved = true; break;
            case 6: vType = QStringLiteral("HELI");       mCount = 1; resolved = true; break;
            case 7: vType = QStringLiteral("TRI");        mCount = 3; resolved = true; break;
            case 0: vType = QStringLiteral("FIXED_WING"); mCount = 1; resolved = true; break;
            default: break;
            }
        }
    }

    // PX4 priority: CA_AIRFRAME
    if (!resolved && (v->px4Firmware() || m_telemetry->hasParameter(QStringLiteral("CA_AIRFRAME")))) {
        if (m_telemetry->hasParameter(QStringLiteral("CA_AIRFRAME"))) {
            int ca = static_cast<int>(m_telemetry->parameterValue(QStringLiteral("CA_AIRFRAME"), -1.0f));
            if (ca == 0) {
                int rc = m_telemetry->hasParameter(QStringLiteral("CA_ROTOR_CNT"))
                             ? static_cast<int>(m_telemetry->parameterValue(QStringLiteral("CA_ROTOR_CNT"), 4.0f))
                             : 4;
                mCount = rc;
                if (rc == 6) vType = QStringLiteral("HEX");
                else if (rc == 8) vType = QStringLiteral("OCTA");
                else if (rc == 3) vType = QStringLiteral("TRI");
                else vType = QStringLiteral("QUAD");
                resolved = true;
            } else if (ca == 1 || ca == 2) {
                vType = QStringLiteral("FIXED_WING");
                mCount = 1;
                resolved = true;
            } else if (ca == 4 || ca == 5) {
                vType = QStringLiteral("VTOL");
                mCount = 5;
                resolved = true;
            } else if (ca == 6 || ca == 7) {
                vType = QStringLiteral("ROVER");
                mCount = 0;
                resolved = true;
            }
        }
    }

    // Fallback: HEARTBEAT.type
    if (!resolved) {
        int mavType = v->vehicleType();
        if (v->fixedWing() || mavType == 1) {
            vType = QStringLiteral("FIXED_WING"); mCount = 1;
        } else if (mavType == 2) {
            vType = QStringLiteral("QUAD"); mCount = 4;
        } else if (mavType == 13) {
            vType = QStringLiteral("HEX"); mCount = 6;
        } else if (mavType == 14) {
            vType = QStringLiteral("OCTA"); mCount = 8;
        } else if (mavType == 15) {
            vType = QStringLiteral("TRI"); mCount = 3;
        } else if (v->vtol() || (mavType >= 19 && mavType <= 24)) {
            vType = QStringLiteral("VTOL"); mCount = 4;
        } else if (v->multiRotor()) {
            vType = QStringLiteral("QUAD"); mCount = 4;
        }
        if (m_telemetry->parametersReady()) {
            resolved = true;
        }
    }

    m_vehicleType = vType;
    m_kind = kindFromTypeString(vType);
    m_motorCount = mCount;

    if (resolved && !m_typeResolved) {
        m_typeResolved = true;
        emit vehicleTypeResolved();
    }
}

/// Called when the telemetry link connects or disconnects.
/// On disconnect: ends the flight session, accumulates flight hours, saves last GPS.
/// On connect: resolves device UID, upserts profile in DB, starts a new flight session.
void VehicleProfileManager::onConnectionChanged()
{
    if (!m_telemetry || !m_telemetry->isConnected()) {
        m_liveTimer.stop();
        if (m_liveSOC != -1 || m_liveTimeMins != -1.0 || !m_powerLive.isEmpty()) {
            m_liveSOC = -1;
            m_liveTimeMins = -1.0;
            m_liveSmoothAmps = 0.0;
            m_powerLive.clear();
            emit liveDataChanged();
        }
        m_liveWasArmed = false;
        m_livePayloadAboard = false;
        m_powerModelDirty = true;
        m_liveSeenDisarmed = false;
        m_gcsCounting = false;
        m_gcsCountKnown = false;
        m_gcsUsedAh = 0.0;
        m_gcsLastMs = -1;
        // The arming-gate flow owns flight sessions end-to-end (opens on gate
        // up, closes on disarm / check failure) and owns the per-flight
        // counters; the session id is reset here for the next connect.
        m_flightSessionId = -1;
        // Auto-save the vehicle's last known GPS position on disconnect.
        if (!m_currentDeviceUid.isEmpty()) {
            double lat = m_telemetry ? m_telemetry->gpsLatitude() : 0.0;
            double lon = m_telemetry ? m_telemetry->gpsLongitude() : 0.0;
            if (qAbs(lat) > 0.1 || qAbs(lon) > 0.1)
                DatabaseManager::instance().updateVehicleGps(m_currentDeviceUid, lat, lon);
        }
        m_currentDeviceUid.clear();
        m_currentVehicleHistoryJson.clear();
        if (m_uavWeightKg != 0.0) {
            m_uavWeightKg = 0.0;
            emit uavWeightChanged();
        }
        // Battery pack config is per-vehicle; reset to defaults while disconnected.
        m_batterySeriesCells = 6;
        m_batteryParallelCells = 1;
        m_batteryCellMah = 5000;
        m_batteryReserve = 0.20;
        m_batteryType = QStringLiteral("LiPo");
        emit batteryConfigChanged();
        // No vehicle connected — fall back to the built-in table.
        _applyDefaultThrustTable();
        if (m_powerTypeId != -1 || !m_motorTableId.isEmpty()) {
            m_powerTypeId = -1;
            m_motorTableId.clear();
            m_packOhm = -1.0;
            emit powerTypeChanged();
        }
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

    // Restore the persisted empty-airframe weight for this vehicle
    m_uavWeightKg = DatabaseManager::instance().vehicleUavWeight(m_currentDeviceUid);
    emit uavWeightChanged();

    // Restore the persisted battery pack config for the live time estimate
    DatabaseManager::instance().vehicleBatteryConfig(m_currentDeviceUid,
        &m_batterySeriesCells, &m_batteryParallelCells,
        &m_batteryCellMah, &m_batteryReserve, &m_batteryType);
    emit batteryConfigChanged();

    // Restore the persisted thrust→current datasheet (or the default table)
    _loadPersistedThrustTable(m_currentDeviceUid);

    m_powerTypeId = DatabaseManager::instance().vehiclePowerTypeId(m_currentDeviceUid);
    m_motorTableId = DatabaseManager::instance().vehicleMotorTableId(m_currentDeviceUid);
    m_packOhm = QSettings().value(QStringLiteral("MotorTable/packOhm/%1").arg(m_currentDeviceUid), -1.0).toDouble();
    emit powerTypeChanged();

    // The flight session itself is opened by the arming-gate flow
    // (PreflightPlugin::_onGateOpened) — the single session owner — which
    // hands this controller the session id so live battery energy can be
    // attached to the one and only row per flight.  Previously every telemetry
    // connect also opened a flight_sessions row here, producing two sessions
    // per flight and splitting history across deviceUid and fingerprint keys.
    m_flightSessionId = -1;

    // Kick off the live battery estimate immediately, then every 2 s.
    _updateLiveEstimate();
    m_liveTimer.start();

    emit currentVehicleChanged();
}

/// Resolve the persistent device identifier for the connected vehicle.
/// Must match the key VehicleRegistry/registerNewVehicle uses, so battery
/// history, flight sessions and the vehicle registry all land on ONE row:
///   hardware UID (decimal, matching QString::number(AUTOPILOT_VERSION.uid))
///   > connection fingerprint (VehicleRegistry) > MAV_SYS_ID.
QString VehicleProfileManager::resolveDeviceUid()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};

    // Primary: hardware UID from AUTOPILOT_VERSION.uid — the same decimal
    // string VehicleRegistry::registerNewVehicle keys the registry row by.
    quint64 uid = v->vehicleUID();
    if (uid != 0) {
        return QString::number(uid);
    }

    // Fallback: the connection-derived fingerprint — identical to the key the
    // vehicle registry row uses for UID-less (e.g. SITL) vehicles.
    QString fp = VehicleRegistry::instance()->currentFingerprint();
    if (!fp.isEmpty()) {
        return fp;
    }

    // Last resort: MAV_SYS_ID (session-only, clearly non-ideal)
    return QStringLiteral("sysid:%1").arg(m_telemetry->vehicle()->id());
}

// Resolve motor count from vehicle firmware parameters.
// PX4 uses CA_AIRFRAME; ArduPilot uses FRAME_CLASS + FRAME_TYPE.
// Falls back to defaultCount if parameters are missing or unrecognized.
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

// Return "PX4", "ArduPilot", or "Generic" based on the connected vehicle's firmware.
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
        m_packWaivedBy.clear();   // a chosen pack replaces any waiver
    }
    emit currentBatteryChanged();
}

// The waiver satisfies the "Battery pack" item (as a warning) so the gate
// lets the arm through, and the Fly-view battery chip shows no flight-time
// estimate: the stored S/P/mAh may belong to some other pack. A flight with
// no serial is never counted against any pack's cycles.
bool VehicleProfileManager::waiveBatteryPack(const QString &pilot)
{
    const QString name = pilot.trimmed();
    if (name.isEmpty())
        return false;
    m_packWaivedBy = name;

    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    QJsonObject entry;
    entry[QStringLiteral("event")] = QStringLiteral("battery_pack_waived");
    entry[QStringLiteral("pilot")] = name;
    entry[QStringLiteral("reason")] = QStringLiteral("No registered pack; no flight-time estimate");
    entry[QStringLiteral("checks")] = QJsonArray{QStringLiteral("power.battery.pack")};
    entry[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    DatabaseManager::instance().saveComplianceLog(
        QStringLiteral("gate-battery_pack_waived-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        // compliance_logs.vehicle_id is NOT NULL: a waiver given before the
        // vehicle connects is still kept, under "unknown".
        v ? QString::number(v->id()) : QStringLiteral("unknown"),
        v ? v->vehicleTypeString() : QStringLiteral("unknown"), name,
        QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)), QString());

    emit currentBatteryChanged();
    return true;
}

void VehicleProfileManager::clearBatteryPackWaiver()
{
    if (m_packWaivedBy.isEmpty())
        return;
    m_packWaivedBy.clear();
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

// Update payload weight, clamping to non-negative. Persists to active flight session if one exists.
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

bool VehicleProfileManager::enterPayloadWeightKg(double kg)
{
    if (!std::isfinite(kg) || kg < 0.0)
        return false;
    const bool wasEntered = m_payloadEntered;
    m_payloadEntered = true;
    // qFuzzyCompare cannot compare against 0, hence the +1.
    if (!qFuzzyCompare(m_payloadWeightKg + 1.0, kg + 1.0))
        setPayloadWeightKg(kg);       // emits; also updates an open flight's record
    else if (!wasEntered)
        emit payloadWeightChanged();
    return true;
}

void VehicleProfileManager::clearPayloadEntry()
{
    if (!m_payloadEntered && m_payloadWeightKg == 0.0 && m_timeAtTargetMin < 0.0)
        return;
    m_payloadEntered = false;
    m_payloadWeightKg = 0.0;
    m_timeAtTargetMin = -1.0;
    emit payloadWeightChanged();
}

bool VehicleProfileManager::enterTimeAtTargetMin(double minutes)
{
    if (!std::isfinite(minutes) || minutes < 0.0 || minutes > 600.0)
        return false;
    m_timeAtTargetMin = minutes;
    emit payloadWeightChanged();
    return true;
}

void VehicleProfileManager::setUavWeight(double value, const QString &unit)
{
    double kg = value;
    if (unit.compare(QStringLiteral("lbs"), Qt::CaseInsensitive) == 0)
        kg = value * 0.453592;
    // Sanity clamp: 50 g .. 500 kg
    kg = qBound(0.05, kg, 500.0);
    if (qFuzzyCompare(m_uavWeightKg, kg)) return;
    m_uavWeightKg = kg;
    if (!m_currentDeviceUid.isEmpty())
        DatabaseManager::instance().updateVehicleUavWeight(m_currentDeviceUid, m_uavWeightKg);
    emit uavWeightChanged();
}

void VehicleProfileManager::setBatterySeriesCells(int cells)
{
    cells = qBound(1, cells, 24);
    if (m_batterySeriesCells == cells) return;
    m_batterySeriesCells = cells;
    _persistBatteryConfig();
    _updateLiveEstimate();
    emit batteryConfigChanged();
}

void VehicleProfileManager::setBatteryParallelCells(int cells)
{
    cells = qBound(1, cells, 12);
    if (m_batteryParallelCells == cells) return;
    m_batteryParallelCells = cells;
    _persistBatteryConfig();
    _updateLiveEstimate();
    emit batteryConfigChanged();
}

void VehicleProfileManager::setBatteryCellMah(int mah)
{
    mah = qBound(250, mah, 60000);
    if (m_batteryCellMah == mah) return;
    m_batteryCellMah = mah;
    _persistBatteryConfig();
    _updateLiveEstimate();
    emit batteryConfigChanged();
}

void VehicleProfileManager::setBatteryReserve(double reserve)
{
    reserve = qBound(0.0, reserve, 0.9);
    if (qFuzzyCompare(m_batteryReserve, reserve)) return;
    m_batteryReserve = reserve;
    _persistBatteryConfig();
    _updateLiveEstimate();
    emit batteryConfigChanged();
}

void VehicleProfileManager::setBatteryType(const QString &type)
{
    QString t = type.trimmed();
    if (t.isEmpty()) return;
    if (m_batteryType == t) return;
    m_batteryType = t;
    _persistBatteryConfig();
    _updateLiveEstimate();
    emit batteryConfigChanged();
}

// Force arm only through the single funnel (TelemetryBridge::forceArm), which
// checks the session, ground speed and pilot details, bounds the override and
// records it. Without the bridge there is no safe path, so nothing is sent.
void VehicleProfileManager::forceArm()
{
    if (!m_telemetry) {
        qWarning() << "VehicleProfileManager: force arm refused, no telemetry bridge";
        return;
    }
    qInfo() << "VehicleProfileManager: Force arming vehicle (funnel)";
    m_telemetry->forceArm(QStringLiteral("VehicleProfileManager"),
                          QStringLiteral("Profile force-arm action"));
}

void VehicleProfileManager::disarmVehicle(bool force)
{
    Vehicle *vehicle = MultiVehicleManager::instance() ? MultiVehicleManager::instance()->activeVehicle() : nullptr;
    if (vehicle) {
        qInfo() << "VehicleProfileManager: Disarming vehicle (force =" << force << ")";
        if (force) {
            vehicle->sendMavCommand(vehicle->defaultComponentId(),
                                   MAV_CMD_COMPONENT_ARM_DISARM,
                                   true,    // showError
                                   0.0f,    // 0.0 = disarm
                                   kForceDisarmParam2); // ArduPilot force-DISARM (21196); 2989 is force-arm
        } else {
            // Disarm, not arm. Prior code erroneously passed `true` which
            // requested arming from a "disarm" entry point and, with the new
            // Vehicle-level arm interceptor in place, would additionally be
            // vetoed by the gate.
            vehicle->setArmedShowError(false);
        }
    }
}

void VehicleProfileManager::setVehicleFlightMode(const QString &mode)
{
    Vehicle *vehicle = MultiVehicleManager::instance() ? MultiVehicleManager::instance()->activeVehicle() : nullptr;
    if (!vehicle) {
        qWarning() << "VehicleProfileManager::setVehicleFlightMode: No active vehicle";
        return;
    }

    QStringList availableModes = vehicle->flightModes();
    QString targetMode = mode;

    // Direct case-insensitive match
    for (const QString &m : availableModes) {
        if (m.compare(mode, Qt::CaseInsensitive) == 0) {
            targetMode = m;
            goto execute_set_mode;
        }
    }

    // Fallback translation if exact string is not directly in flightModes
    if (mode.compare("MANUAL", Qt::CaseInsensitive) == 0) {
        static const QStringList manualCandidates = {
            QStringLiteral("MANUAL"), QStringLiteral("Manual"),
            QStringLiteral("STABILIZE"), QStringLiteral("Stabilize"), QStringLiteral("Stabilized"),
            QStringLiteral("ALT_HOLD"), QStringLiteral("AltHold"),
            QStringLiteral("POSHOLD"), QStringLiteral("PosHold"), QStringLiteral("Position"),
            QStringLiteral("Loiter"), QStringLiteral("LOITER")
        };
        for (const QString &cand : manualCandidates) {
            for (const QString &avail : availableModes) {
                if (avail.compare(cand, Qt::CaseInsensitive) == 0) {
                    targetMode = avail;
                    goto execute_set_mode;
                }
            }
        }
    } else if (mode.compare("AUTO", Qt::CaseInsensitive) == 0) {
        static const QStringList autoCandidates = {
            QStringLiteral("AUTO"), QStringLiteral("Auto"),
            QStringLiteral("GUIDED"), QStringLiteral("Guided"),
            QStringLiteral("MISSION"), QStringLiteral("Mission")
        };
        for (const QString &cand : autoCandidates) {
            for (const QString &avail : availableModes) {
                if (avail.compare(cand, Qt::CaseInsensitive) == 0) {
                    targetMode = avail;
                    goto execute_set_mode;
                }
            }
        }
    }

execute_set_mode:
    if (!availableModes.contains(targetMode)) {
        qWarning() << "VehicleProfileManager: flight mode" << targetMode
                   << "is not in the vehicle's available modes" << availableModes
                   << "- the request will be rejected";
    }
    // Request only — Vehicle::setFlightMode() returns void and warns itself if the
    // firmware plugin cannot command the mode, so do not log this as an outcome.
    qInfo() << "VehicleProfileManager: requesting flight mode" << targetMode;
    vehicle->setFlightMode(targetMode);
}

void VehicleProfileManager::_persistBatteryConfig()
{
    if (m_currentDeviceUid.isEmpty()) return;
    DatabaseManager::instance().updateVehicleBatteryConfig(
        m_currentDeviceUid, m_batterySeriesCells, m_batteryParallelCells,
        m_batteryCellMah, m_batteryReserve, m_batteryType);
}

// ── Live battery estimate (Feeds the FlyView chip) ────────────────────────
// Runs every 2 s from TelemetryBridge telemetry while a vehicle is connected:
//   I_smooth  = 0.095·I + 0.905·I_smooth    (EMA, α=0.095 @ 0.5 Hz ≈ 10 s)
//   usable_Ah    = capacity_Ah · (SOC/100 − reserve), never below 0
//                  (reserve = share of the whole pack, default 20 %)
//   time_min     = usable_Ah / I_smooth · 60, gated on armed && I_smooth >= 3 A;
//                  0 means "at the reserve: land now"
//   (EMA α = 0.095 per 2 s sample: time constant ≈ 20 s)
void VehicleProfileManager::_updateLiveEstimate()
{
    if (!m_telemetry) return;

    const double kMinGateA = 3.0;
    const double kAlpha = 0.095;

    // The selected pack's charge from the FC's mAh counter (battery 1); the
    // FC's own % (relative to BATT_CAPACITY) only when that counter is missing.
    double mahUsed = -1.0;
    if (Vehicle *v = m_telemetry->vehicle()) {
        QmlObjectListModel *batteries = v->batteries();
        if (batteries && batteries->count() > 0) {
            if (auto *b = batteries->value<VehicleBatteryFactGroup *>(0)) {
                const QVariant mah = b->mahConsumed()->rawValue();
                if (mah.isValid())
                    mahUsed = mah.toDouble();
            }
        }
    }
    int    soc = packSocPct(m_telemetry->batterySocPct(), mahUsed, capacityAh());
    double cur = m_telemetry->batteryCurrentAmps();
    bool   armed = m_telemetry->armed();
    bool   connected = m_telemetry->isConnected();

    // EMA current smoothing with instant first-sample initialization.
    if (cur > 0.0) {
        if (m_liveSmoothAmps <= 0.01) {
            m_liveSmoothAmps = cur;
        } else {
            m_liveSmoothAmps = kAlpha * cur + (1.0 - kAlpha) * m_liveSmoothAmps;
        }
    } else {
        m_liveSmoothAmps = (1.0 - kAlpha) * m_liveSmoothAmps;   // smooth decay toward 0
    }

    double timeMin = -1.0;
    if (connected && soc >= 0 && capacityAh() > 0.0
            && armed && m_liveSmoothAmps >= kMinGateA) {
        const double usableAh = usableAhAboveReserve(capacityAh(), soc, m_batteryReserve);
        timeMin = (usableAh / m_liveSmoothAmps) * 60.0;   // 0 at/below the reserve
    }

    if (soc != m_liveSOC || !qFuzzyCompare(timeMin + 1, m_liveTimeMins + 1)) {
        m_liveSOC = soc;
        m_liveTimeMins = timeMin;
        emit liveDataChanged();
    }
    _updatePowerLive(mahUsed, m_telemetry->batterySocPct());
}

// The power-type estimate for the Fly view (PowerTypeModel::liveEstimate):
// the linked type's model (re-fitted when its flights change), the selected
// pack, the FC's mAh used × the charger factor, the smoothed FC current, the
// copter's position, home, and the target saved on the gimbal page. The
// payload counts as aboard from the arm until the copter first comes within
// 20 m of the target.
QVariantMap VehicleProfileManager::airDensity() const
{
    QVariantMap out;
    double p = m_telemetry ? m_telemetry->baroPressure() : qQNaN();
    QString pressureSource = QStringLiteral("barometer");
    if (!(p > 300.0 && p < 1100.0)) {
        const double alt = m_telemetry ? m_telemetry->gpsAltitude() : qQNaN();
        if (std::isfinite(alt) && alt > -500.0 && alt < 9000.0 && alt != 0.0) {
            p = MotorTable::isaPressureHpa(alt);
            pressureSource = QStringLiteral("GPS altitude");
        } else {
            out[QStringLiteral("ratio")] = MotorTable::kTableDensityRatio;
            out[QStringLiteral("source")] = QStringLiteral("not known: sea level assumed");
            return out;
        }
    }
    WeatherProvider *wp = WeatherProvider::instance();
    double tempC = wp ? wp->temperature() : qQNaN();
    double humidity = wp ? wp->humidity() : qQNaN();
    QString tempSource = QStringLiteral("weather");
    if (!(tempC > -60.0 && tempC < 60.0)) {
        tempC = MotorTable::isaTempC(MotorTable::isaAltitudeM(p));
        humidity = qQNaN();
        tempSource = QStringLiteral("standard atmosphere");
    }
    out[QStringLiteral("ratio")] = MotorTable::densityRatio(p, tempC, humidity);
    out[QStringLiteral("pressureHpa")] = p;
    out[QStringLiteral("tempC")] = tempC;
    out[QStringLiteral("humidityPct")] = humidity;
    out[QStringLiteral("source")] = pressureSource + QStringLiteral(" + ") + tempSource;
    return out;
}

MotorTable::Calibration VehicleProfileManager::motorTableCalibrationNow() const
{
    QVector<MotorTable::TableFlight> flights;
    if (!m_motorTableId.isEmpty())
        for (const QVariant &v : DatabaseManager::instance().motorTableFlights(m_currentDeviceUid, m_motorTableId)) {
            const QVariantMap m = v.toMap();
            MotorTable::TableFlight f;
            f.gcsAh = m.value(QStringLiteral("gcsAh")).toDouble();
            f.chargerAh = m.value(QStringLiteral("chargerMah")).toDouble() / 1000.0;
            f.startedFull = m.value(QStringLiteral("startedFull")).toBool();
            f.dismissed = m.value(QStringLiteral("state")).toString() == QLatin1String("dismissed");
            flights.append(f);
        }
    return MotorTable::calibrate(flights);
}

QVariantMap VehicleProfileManager::motorTableCalibration() const
{
    const MotorTable::Calibration c = motorTableCalibrationNow();
    QVariantMap out;
    out[QStringLiteral("usable")] = c.usable;
    out[QStringLiteral("factor")] = c.factor;
    out[QStringLiteral("proven")] = c.proven;
    out[QStringLiteral("needed")] = std::max(0, MotorTable::kProofFlights - c.usable);
    return out;
}

QVariantList VehicleProfileManager::motorTableFlights() const
{
    QVariantList out;
    if (m_motorTableId.isEmpty())
        return out;
    for (const QVariant &v : DatabaseManager::instance().motorTableFlights(m_currentDeviceUid, m_motorTableId)) {
        QVariantMap m = v.toMap();
        MotorTable::TableFlight f;
        f.gcsAh = m.value(QStringLiteral("gcsAh")).toDouble();
        f.chargerAh = m.value(QStringLiteral("chargerMah")).toDouble() / 1000.0;
        f.startedFull = m.value(QStringLiteral("startedFull")).toBool();
        f.dismissed = m.value(QStringLiteral("state")).toString() == QLatin1String("dismissed");
        m[QStringLiteral("usable")] = f.usable();
        m[QStringLiteral("ratio")] = f.chargerAh > 0.0 && f.gcsAh > 0.0 ? f.ratio() : -1.0;
        out.prepend(m);
    }
    return out;
}

bool VehicleProfileManager::setMotorTableFlightCharger(int flightId, double chargerMah)
{
    if (!DatabaseManager::instance().setMotorTableFlightCharger(flightId, chargerMah))
        return false;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::dismissMotorTableFlight(int flightId)
{
    if (!DatabaseManager::instance().setMotorTableFlightState(flightId, QStringLiteral("dismissed")))
        return false;
    emit powerTypeChanged();
    return true;
}

MotorTable::Pack VehicleProfileManager::motorTablePack(int cells) const
{
    MotorTable::Pack p;
    p.cells = cells;
    p.chemistry = m_batteryType;
    p.capacityAh = capacityAh();
    p.resistanceOhm = m_packOhm > 0.0 ? m_packOhm : cells * MotorTable::kDefaultCellGroupOhm;
    return p;
}

QVariantMap VehicleProfileManager::motorTableSetup() const
{
    QVariantMap out;
    const MotorTable::Table *table = m_kind == VehicleKind::Multirotor ? MotorTable::byId(m_motorTableId) : nullptr;
    out[QStringLiteral("linked")] = table != nullptr;
    if (!table || !m_telemetry)
        return out;
    auto param = [this](const QString &name, double fallback) {
        return m_telemetry->hasParameter(name) ? static_cast<double>(m_telemetry->parameterValue(name)) : fallback;
    };
    out[QStringLiteral("tableName")] = table->name;

    // Motors from FRAME_CLASS, else the outputs set to motor functions.
    const QVariantMap mt = motorThrottleStatus();
    int motors = mt.value(QStringLiteral("motorsExpected")).toInt();
    if (motors <= 0)
        motors = mt.value(QStringLiteral("channels")).toList().size();
    const int motorsUsed = motors > 0 ? motors : table->motors;
    out[QStringLiteral("motors")] = motors;

    // Cells from ArduPilot's voltage compensation (MOT_BAT_VOLT_MAX is set to
    // the full pack, 4.2 V per cell), else the pack entered at preflight.
    int cells = MotorTable::cellsFromVoltMax(param(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0));
    QString cellsSource = cells > 0 ? QStringLiteral("MOT_BAT_VOLT_MAX") : QString();
    if (cells <= 0 && m_batterySeriesCells > 0) {
        cells = m_batterySeriesCells;
        cellsSource = QStringLiteral("pack");
    }
    out[QStringLiteral("cells")] = cells;
    out[QStringLiteral("cellsSource")] = cellsSource;
    // A single voltage for the pack, where one is needed (the hover throttle,
    // and turning MOT_THST_HOVER into thrust): its resting voltage at half
    // charge on its chemistry's curve (Li-ion 3.73 V per cell, LiPo 3.84 V).
    const double planV = (cells > 0 ? cells : table->cells) * PowerTypeModel::restingCellVolts(50.0, m_batteryType);
    // The air where the copter is.
    const QVariantMap air = airDensity();
    const double sigma = air.value(QStringLiteral("ratio"), MotorTable::kTableDensityRatio).toDouble();
    out[QStringLiteral("densityRatio")] = sigma;
    out[QStringLiteral("densitySource")] = air.value(QStringLiteral("source"));
    out[QStringLiteral("planVolts")] = planV;

    // The hover: the last flight's steady hover, else the hover throttle the
    // FC has learned (not the untouched default) turned into motor output the
    // way ArduPilot does.
    double thrust = -1.0;
    QString source;
    const QVariantMap stored = DatabaseManager::instance().vehicleMotorHover(m_currentDeviceUid);
    if (!stored.isEmpty()) {
        thrust = stored.value(QStringLiteral("thrustG")).toDouble();
        source = QStringLiteral("last flight");
    } else {
        const double learn = param(QStringLiteral("MOT_HOVER_LEARN"), 0.0);
        const double h = param(QStringLiteral("MOT_THST_HOVER"), -1.0);
        if (learn >= 1.0 && h > 0.0 && std::abs(h - MotorTable::kDefaultThstHover) > 1e-4) {
            MotorTable::HoverParams p;
            p.thstHover = h;
            p.spinMin = param(QStringLiteral("MOT_SPIN_MIN"), p.spinMin);
            p.spinMax = param(QStringLiteral("MOT_SPIN_MAX"), p.spinMax);
            p.expo = param(QStringLiteral("MOT_THST_EXPO"), p.expo);
            p.battVoltMax = param(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0);
            p.battVoltMin = param(QStringLiteral("MOT_BAT_VOLT_MIN"), 0.0);
            const double act = MotorTable::hoverMotorThrottle(p, planV);
            if (act > 0.0) {
                thrust = MotorTable::thrustAt(*table, act * planV / table->volts).grams
                         * MotorTable::densityThrustRatio(*table, act * planV, sigma);
                source = QStringLiteral("MOT_THST_HOVER");
            }
        }
    }
    double hoverThrottle = -1.0;
    double thrustToWeight = -1.0;
    double hoverAvgA = -1.0;
    double hoverPeakA = -1.0;
    out[QStringLiteral("hoverSource")] = thrust > 0.0 ? source : QString();
    // The hover current through the rest of the pack: the motors' power for
    // that thrust, drawn from the pack's charge now (from its resting voltage)
    // down to the reserve, at the voltage the pack has at each step (its curve
    // and its drop under load). Planned at a fixed voltage only when the pack
    // is not known.
    const MotorTable::Pack pack = motorTablePack(cells > 0 ? cells : table->cells);
    const double socNow = packRestingSocPct(m_telemetry->batteryVoltage());
    const double socReserve = m_batteryReserve * 100.0;
    // The fine-tuning from the charger comparison multiplies the table's
    // current (1 until two flights have a charger number).
    const MotorTable::Calibration cal = motorTableCalibrationNow();
    out[QStringLiteral("calibrationFactor")] = cal.factor;
    out[QStringLiteral("calibrationFlights")] = cal.usable;
    out[QStringLiteral("proven")] = cal.proven;
    auto hoverAt = [&](double thrustG) {
        const double watts = motorsUsed * MotorTable::powerForThrustAt(*table, thrustG, sigma).watts;
        const double avg = MotorTable::averageCurrent(pack, watts, socNow, socReserve);
        return cal.factor * (avg > 0.0 ? avg : MotorTable::hoverCurrent(*table, thrustG, motorsUsed, planV));
    };
    out[QStringLiteral("packOhm")] = pack.resistanceOhm;
    out[QStringLiteral("packOhmLearned")] = m_packOhm > 0.0;
    if (thrust > 0.0) {
        const double hoverA = hoverAt(thrust);
        const double watts = motorsUsed * MotorTable::powerForThrustAt(*table, thrust, sigma).watts;
        hoverThrottle = MotorTable::hoverThrottleAtDensity(*table, thrust, planV, sigma);
        out[QStringLiteral("hoverThrustG")] = thrust;
        out[QStringLiteral("weightKg")] = thrust * motorsUsed / 1000.0;
        out[QStringLiteral("hoverWatts")] = watts;
        out[QStringLiteral("hoverA")] = hoverA;
        const double reserveVolts = MotorTable::loadedVolts(pack, socReserve, watts);
        out[QStringLiteral("reserveVolts")] = reserveVolts;
        // The hover current by the reserve: the pack's lowest voltage in a
        // normal flight, so its highest current.
        hoverPeakA = cal.factor * MotorTable::packCurrent(
            watts, pack.cells * PowerTypeModel::restingCellVolts(socReserve, pack.chemistry), pack.resistanceOhm);
        hoverAvgA = hoverA;
        out[QStringLiteral("hoverPeakA")] = hoverPeakA;
        // Spare thrust at the end of a normal flight: full-throttle thrust at
        // the reserve voltage, in this air, ÷ the weight (with the entered
        // payload).
        const double weightThrust = thrust + ((m_payloadEntered && m_payloadWeightKg > 0.0)
                                                  ? m_payloadWeightKg * 1000.0 / motorsUsed : 0.0);
        if (reserveVolts > 0.0) {
            const MotorTable::ThrustAt maxT = MotorTable::maxThrustAt(*table, reserveVolts, sigma);
            thrustToWeight = maxT.grams / weightThrust;
            out[QStringLiteral("thrustToWeight")] = thrustToWeight;
            out[QStringLiteral("maxThrustG")] = maxT.grams;
        }
        // A payload adds its weight shared over the motors. The trip is
        // costed at two hovers: with the entered payload aboard (to the
        // target) and without it (after the drop). Both are read from the
        // table at their real thrust; the amps per kg is the line through
        // those two, so both legs are exact (the table curves upwards, so
        // one slope worked out at 1 kg would be low for a heavier payload).
        const double payloadKg = (m_payloadEntered && m_payloadWeightKg > 0.0) ? m_payloadWeightKg : 1.0;
        const double withPayloadA = hoverAt(thrust + payloadKg * 1000.0 / motorsUsed);
        out[QStringLiteral("hoverAPerKg")] = (withPayloadA - hoverA) / payloadKg;
        out[QStringLiteral("hoverAWithPayload")] = withPayloadA;
        out[QStringLiteral("payloadKgCosted")] = payloadKg;
        // Cruise at the copter's own mission speed (WP_SPD): the hover power
        // × the cruise ratio measured on this copter at that speed, through
        // the rest of the pack; the hover itself until a flight has measured
        // it (at speed drag is large, so cruise may need more than hover, not
        // less). No speed is assumed: until WP_SPD is read it is not known.
        const double cruiseSpeed = MissionWalk::missionSpeedMs(copterParams(m_telemetry));
        const double stored = storedCruiseRatio(m_currentDeviceUid, cruiseSpeed);
        const bool cruiseMeasured = stored > 0.0;
        const double ratio = cruiseMeasured ? stored : 1.0;
        auto cruiseAt = [&](double thrustG) {
            const double watts = ratio * motorsUsed * MotorTable::powerForThrustAt(*table, thrustG, sigma).watts;
            const double avg = MotorTable::averageCurrent(pack, watts, socNow, socReserve);
            return avg > 0.0 ? cal.factor * avg : hoverAt(thrustG) * ratio;
        };
        const double cruiseA = cruiseAt(thrust);
        out[QStringLiteral("cruiseA")] = cruiseA;
        out[QStringLiteral("cruiseAPerKg")] = (cruiseAt(thrust + payloadKg * 1000.0 / motorsUsed) - cruiseA) / payloadKg;
        out[QStringLiteral("cruiseRatio")] = ratio;
        out[QStringLiteral("cruiseMeasured")] = cruiseMeasured;
        out[QStringLiteral("cruiseSpeedMs")] = cruiseSpeed;
        out[QStringLiteral("hoverThrottle")] = hoverThrottle;
    }

    MotorTable::SetupFacts f;
    f.motors = motors;
    f.cells = cells;
    f.capacityAh = capacityAh();
    f.thrustToWeight = thrustToWeight;
    f.hoverAvgA = hoverAvgA;
    f.hoverPeakA = hoverPeakA;
    f.ratingA = table->packCRating * capacityAh();
    out[QStringLiteral("ratingA")] = f.ratingA;
    f.outputsFresh = mt.value(QStringLiteral("fresh")).toBool();
    QVariantList issues;
    bool off = false;
    for (const MotorTable::SetupIssue &i : MotorTable::setupIssues(*table, f)) {
        QVariantMap m;
        m[QStringLiteral("level")] = i.level;
        m[QStringLiteral("text")] = i.text;
        issues.append(m);
        off = off || i.level == QLatin1String("off");
    }
    out[QStringLiteral("issues")] = issues;
    out[QStringLiteral("off")] = off;
    return out;
}

// One motor-output reading for a copter on a motor table: the battery
// current for the four motors' throttles at the pack voltage and the air
// density, counted for the time since the previous reading (no more than
// kMaxReadingGapMs, so a dropout is not filled with the last value). The 2 s
// update then uses these readings' average power instead of only the latest
// one: throttle jumps around in gusts, and current grows faster than throttle,
// so every reading counts.
void VehicleProfileManager::_onMotorOutputs()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastMs = m_lastReadingMs;
    m_lastReadingMs = nowMs;
    const MotorTable::Table *table = m_kind == VehicleKind::Multirotor ? MotorTable::byId(m_motorTableId) : nullptr;
    if (!table || !m_telemetry || !m_telemetry->armed())
        return;
    QVector<double> thr;
    for (const QVariant &t : motorThrottleStatus().value(QStringLiteral("throttles")).toList())
        thr.append(t.toDouble());
    if (thr.isEmpty())
        return;
    const double packV = m_telemetry->batteryVoltage();
    const MotorTable::BatteryCurrent now = MotorTable::batteryCurrent(*table, thr, packV, m_lastSigma);
    const double v = packV > 0.0 ? packV : table->volts;
    m_readingWattsSum += (now.amps - MotorTable::kElectronicsA) * v;
    ++m_readings;
    if (m_gcsCounting && lastMs > 0)
        m_gcsUsedAh += now.amps * std::min(nowMs - lastMs, MotorTable::kMaxReadingGapMs) / 1000.0 / 3600.0;
}

QVariantMap VehicleProfileManager::motorThrottleStatus() const
{
    QVariantMap out;
    if (!m_telemetry || !m_telemetry->vehicle())
        return out;
    auto param = [this](const QString &name, double fallback) {
        return m_telemetry->hasParameter(name) ? static_cast<double>(m_telemetry->parameterValue(name)) : fallback;
    };
    // Which outputs drive motors (SERVOn_FUNCTION 33–40), and how many motors
    // the frame has (FRAME_CLASS).
    QVector<int> functions;
    for (int n = 1; n <= 16; ++n)
        functions.append(qRound(param(QStringLiteral("SERVO%1_FUNCTION").arg(n), 0.0)));
    const QVector<int> channels = MotorTable::motorChannels(functions);
    const int frameClass = qRound(param(QStringLiteral("FRAME_CLASS"), -1.0));
    const int expected = frameClass > 0 ? std::max(0, HardwareTestController::motorCountFromFrameClass(frameClass)) : 0;
    const MotorTable::EscRange range = MotorTable::escRange(param(QStringLiteral("MOT_PWM_MIN"), 0.0),
                                                            param(QStringLiteral("MOT_PWM_MAX"), 0.0));
    QVector<int> outputs;
    for (const QVariant &o : m_telemetry->motorOutputs())
        outputs.append(o.toInt());
    const QVector<double> throttles = MotorTable::motorThrottles(outputs, channels, expected, range);
    const qint64 age = m_telemetry->streamAgeMs(TelemetryStream::ServoOutputRaw);

    QVariantList thr, ch;
    for (double t : throttles)
        thr.append(t);
    for (int c : channels)
        ch.append(c);
    out[QStringLiteral("throttles")] = thr;
    out[QStringLiteral("channels")] = ch;
    out[QStringLiteral("pwmMin")] = range.min;
    out[QStringLiteral("pwmMax")] = range.max;
    out[QStringLiteral("motorsExpected")] = expected;
    out[QStringLiteral("ageMs")] = age;
    out[QStringLiteral("fresh")] = !throttles.isEmpty() && age >= 0 && age <= MotorTable::kMotorOutputStaleMs;
    return out;
}

void VehicleProfileManager::_updatePowerLive(double fcMahUsed, int socPct)
{
    QVariantMap out;
    const bool armed = m_telemetry && m_telemetry->armed();
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    // A copter linked to a motor table uses it (MotorTable.h); otherwise its
    // power type's learned model.
    const MotorTable::Table *table = m_kind == VehicleKind::Multirotor ? MotorTable::byId(m_motorTableId) : nullptr;
    const bool applicable = v && m_kind == VehicleKind::Multirotor && !batteryPackWaived()
                            && (table || (m_powerTypeId > 0 && m_powerTypeWaivedBy.isEmpty()));

    if (applicable && !table && m_powerModelDirty) {
        m_powerSummary = powerTypeSummary(m_powerTypeId);
        double speedSum = 0.0;
        int speedN = 0;
        for (const QVariant &row : DatabaseManager::instance().powerTypeFlights(m_powerTypeId)) {
            const QVariantMap f = row.toMap();
            const double spd = f.value(QStringLiteral("cruiseSpeedMs")).toDouble();
            if (f.value(QStringLiteral("learningState")).toString() == QLatin1String("used") && spd > 0.0) {
                speedSum += spd;
                ++speedN;
            }
        }
        m_powerCruiseSpeedMs = speedN > 0 ? speedSum / speedN : 10.0;
        m_powerModelDirty = false;
    }

    const QVariantMap target = applicable ? DatabaseManager::instance().vehicleTarget(m_currentDeviceUid) : QVariantMap();
    const QGeoCoordinate pos = v ? v->coordinate() : QGeoCoordinate();
    // An arm seen on this connection starts a new flight: the payload is
    // aboard and the GCS's charge count starts at 0. An armed vehicle first
    // seen armed (the GCS joined mid-flight) gives no count: what was used
    // before is unknown.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!armed)
        m_liveSeenDisarmed = true;
    // On the ground (disarmed, motors off) the voltage is the resting one:
    // the charge it gives is where the next flight's count starts.
    if (!armed && m_telemetry) {
        m_armStartSocPct = packRestingSocPct(m_telemetry->batteryVoltage());
        m_armVoltsKnown = m_telemetry->batteryVoltage() > 1.0;
    }
    if (armed && !m_liveWasArmed) {
        m_fcMahAtArm = fcMahUsed;
        m_livePayloadAboard = m_payloadEntered && m_payloadWeightKg > 0.0;
        m_gcsUsedAh = 0.0;
        m_tableSmoothA = 0.0;
        m_tableSmoothW = 0.0;
        m_packOhmLive = -1.0;
        m_packOhmSamples = 0;
        m_cruiseRatioSum = 0.0;
        m_cruiseSamples = 0;
        m_cruiseBands.clear();
        m_hoverThrustSum = 0.0;
        m_hoverSamples = 0;
        m_gcsCounting = true;
        m_gcsCountKnown = m_liveSeenDisarmed;
        m_gcsLastMs = nowMs;
        m_tableLog = TableFlightLog();
        m_tableLog.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_tableLog.startSoc = m_armVoltsKnown ? m_armStartSocPct : -1.0;
        m_tableLog.startedFull = m_armVoltsKnown && m_armStartSocPct >= 100.0;
    }
    if (!armed)
        m_gcsCounting = false;
    // At the disarm a flight with enough steady hover leaves its hover for the
    // next take-off.
    if (!armed && m_liveWasArmed) {
        // A flight that got airborne (not a bench arm), counted from its arm:
        // its record for the charger comparison.
        if (table && m_tableLog.airborne && m_gcsCountKnown && m_gcsUsedAh > 0.0) {
            const TableFlightLog &l = m_tableLog;
            QVariantMap f;
            f[QStringLiteral("deviceUid")] = m_currentDeviceUid;
            f[QStringLiteral("tableId")] = table->id;
            f[QStringLiteral("startedAt")] = l.startedAt;
            f[QStringLiteral("gcsAh")] = m_gcsUsedAh;
            f[QStringLiteral("startedFull")] = l.startedFull;
            f[QStringLiteral("startSoc")] = l.startSoc;
            if (l.hoverS > 0.0)
                f[QStringLiteral("hoverW")] = l.hoverWSum / (l.hoverS / 2.0);
            f[QStringLiteral("hoverS")] = l.hoverS;
            if (m_cruiseSamples > 0)
                f[QStringLiteral("cruiseRatio")] = m_cruiseRatioSum / m_cruiseSamples;
            f[QStringLiteral("cruiseS")] = l.cruiseS;
            if (l.voltN > 0)
                f[QStringLiteral("avgVolts")] = l.voltSum / l.voltN;
            f[QStringLiteral("minVolts")] = l.minVolts;
            f[QStringLiteral("beyondS")] = l.beyondS;
            f[QStringLiteral("overallThrottleS")] = l.overallThrottleS;
            if (l.densityN > 0)
                f[QStringLiteral("density")] = l.densitySum / l.densityN;
            f[QStringLiteral("packOhm")] = m_packOhmLive;
            DatabaseManager::instance().insertMotorTableFlight(f);
        }
        m_tableLog = TableFlightLog();
        if (table && m_hoverSamples >= MotorTable::kMinHoverSamples)
            DatabaseManager::instance().saveVehicleMotorHover(m_currentDeviceUid, m_hoverThrustSum / m_hoverSamples,
                                                              m_hoverSamples);
        m_hoverThrustSum = 0.0;
        m_hoverSamples = 0;
        // Each speed band flown level for long enough (30 s) leaves its
        // cruise ratio for the next take-off; bands not flown keep theirs.
        if (table) {
            for (auto it = m_cruiseBands.cbegin(); it != m_cruiseBands.cend(); ++it)
                if (it.value().second >= MotorTable::kMinHoverSamples)
                    QSettings().setValue(QStringLiteral("MotorTable/cruiseBand/%1/%2").arg(m_currentDeviceUid).arg(it.key()),
                                         it.value().first / it.value().second);
        }
        m_cruiseRatioSum = 0.0;
        m_cruiseSamples = 0;
        m_cruiseBands.clear();
        if (table && m_packOhmSamples >= MotorTable::kMinHoverSamples && m_packOhmLive > 0.0) {
            m_packOhm = m_packOhmLive;
            QSettings().setValue(QStringLiteral("MotorTable/packOhm/%1").arg(m_currentDeviceUid), m_packOhm);
        }
    }
    m_liveWasArmed = armed;
    if (m_livePayloadAboard && !target.isEmpty() && pos.isValid()
        && PowerTypeModel::distanceM(pos.latitude(), pos.longitude(), target.value(QStringLiteral("lat")).toDouble(),
                                     target.value(QStringLiteral("lon")).toDouble()) <= PowerTypeModel::kDropRadiusM)
        m_livePayloadAboard = false;

    // The table's setup checks: a different frame or pack, or no pack size,
    // turns the estimate off.
    const QVariantMap setup = table ? motorTableSetup() : QVariantMap();
    const bool tableReady = applicable && table && !setup.value(QStringLiteral("off")).toBool();
    const bool typeReady = applicable && !table
                           && m_powerSummary.value(QStringLiteral("source")).toString() != QLatin1String("none");
    if (tableReady || typeReady) {
        PowerTypeModel::LiveInput in;
        if (typeReady) {
            in.model.hover0 = m_powerSummary.value(QStringLiteral("hover0")).toDouble();
            in.model.hoverK = m_powerSummary.value(QStringLiteral("hoverK")).toDouble();
            in.model.cruise0 = m_powerSummary.value(QStringLiteral("cruise0")).toDouble();
            in.model.cruiseK = m_powerSummary.value(QStringLiteral("cruiseK")).toDouble();
            in.model.status = PowerTypeModel::FitStatus::PayloadNotModelled;   // usable; the numbers above are the model
        }
        in.capacityAh = capacityAh();
        in.reserve = m_batteryReserve;
        in.socPct = socPct;
        in.startSocPct = m_armStartSocPct;
        in.groundSpeedMs = m_telemetry->groundSpeed();

        const int monitor = m_telemetry->hasParameter(QStringLiteral("BATT_MONITOR"))
            ? qRound(m_telemetry->parameterValue(QStringLiteral("BATT_MONITOR"), -1.0f)) : -1;
        if (tableReady) {
            // Battery current from the motor table: each motor's throttle from
            // its output signal, or, when those stop arriving, the overall
            // throttle the FC reports turned into motor output the way
            // ArduPilot does (the same for every motor). Counted while armed:
            // the table covers the motors idling on the ground too.
            const double packV = m_telemetry->batteryVoltage();
            const QVariantMap mt = motorThrottleStatus();
            QVector<double> thr;
            const bool fromOutputs = mt.value(QStringLiteral("fresh")).toBool();
            if (fromOutputs) {
                for (const QVariant &t : mt.value(QStringLiteral("throttles")).toList())
                    thr.append(t.toDouble());
            } else {
                const QVariant overall = v->throttlePct()->rawValue();
                if (overall.isValid()) {
                    MotorTable::HoverParams p;
                    auto param = [this](const QString &name, double fallback) {
                        return m_telemetry->hasParameter(name) ? static_cast<double>(m_telemetry->parameterValue(name))
                                                               : fallback;
                    };
                    p.thstHover = std::max(0.001, overall.toDouble() / 100.0);
                    p.spinMin = param(QStringLiteral("MOT_SPIN_MIN"), p.spinMin);
                    p.spinMax = param(QStringLiteral("MOT_SPIN_MAX"), p.spinMax);
                    p.expo = param(QStringLiteral("MOT_THST_EXPO"), p.expo);
                    p.battVoltMax = param(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0);
                    p.battVoltMin = param(QStringLiteral("MOT_BAT_VOLT_MIN"), 0.0);
                    const int n = mt.value(QStringLiteral("motorsExpected")).toInt() > 0
                                      ? mt.value(QStringLiteral("motorsExpected")).toInt() : table->motors;
                    thr = QVector<double>(n, MotorTable::hoverMotorThrottle(p, packV));
                }
            }
            // The air where the copter is (thin air changes the prop's load).
            const double sigma = setup.value(QStringLiteral("densityRatio"), MotorTable::kTableDensityRatio).toDouble();
            const MotorTable::BatteryCurrent now = thr.isEmpty() ? MotorTable::BatteryCurrent{}
                                                                 : MotorTable::batteryCurrent(*table, thr, packV, sigma);
            const bool haveNow = armed && !thr.isEmpty();
            // A current sensor (BATT_MONITOR other than 0, 3 or 25) is
            // measured and wins; the table is the count otherwise.
            const bool gcsCount = monitor == -1 || monitor == 0 || monitor == 3 || monitor == 25;
            // Motor-output readings are counted as they arrive (_onMotorOutputs);
            // only from the overall throttle does this update count the charge.
            m_lastSigma = sigma;
            const bool readingsCounted = fromOutputs && m_readings > 0;
            if (gcsCount && m_gcsCounting && m_gcsLastMs > 0 && haveNow && !readingsCounted)
                m_gcsUsedAh += now.amps * (nowMs - m_gcsLastMs) / 1000.0 / 3600.0;
            m_gcsLastMs = nowMs;
            // Steady hover without payload, from the motor outputs, airborne,
            // neither climbing nor sinking: the thrust per motor the next
            // take-off is planned with.
            const double alt = m_telemetry->altitudeRelative();
            const double gsNow = std::isfinite(in.groundSpeedMs) ? in.groundSpeedMs : 0.0;
            const double climb = m_telemetry->verticalSpeed();
            // This flight's record: airborne or not, the pack voltage, time
            // beyond the table and on the overall throttle, the air.
            const double logDtS = (armed && m_tableLog.lastMs > 0) ? (nowMs - m_tableLog.lastMs) / 1000.0 : 0.0;
            m_tableLog.lastMs = armed ? nowMs : -1;
            if (haveNow) {
                if (std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM)
                    m_tableLog.airborne = true;
                if (packV > 0.0) {
                    m_tableLog.voltSum += packV;
                    ++m_tableLog.voltN;
                    m_tableLog.minVolts = m_tableLog.minVolts < 0.0 ? packV : std::min(m_tableLog.minVolts, packV);
                }
                if (now.motorsBeyond > 0)
                    m_tableLog.beyondS += logDtS;
                if (!fromOutputs)
                    m_tableLog.overallThrottleS += logDtS;
                m_tableLog.densitySum += sigma;
                ++m_tableLog.densityN;
            }
            if (haveNow && fromOutputs && !m_livePayloadAboard && packV > 0.0 && std::isfinite(alt)
                && alt > PowerTypeModel::kAirborneAltM && PowerTypeModel::isHover(gsNow)
                && std::isfinite(climb) && std::abs(climb) < MotorTable::kHoverMaxClimbMs) {
                m_hoverThrustSum += MotorTable::averageThrust(*table, thr, packV, sigma);
                ++m_hoverSamples;
                m_tableLog.hoverWSum += (now.amps - MotorTable::kElectronicsA) * packV;
                m_tableLog.hoverS += 2.0;
            }
            // Level cruise at any speed without payload: its power ÷ the power
            // this copter (its remembered hover thrust) needs to hover in this
            // air, kept in the band of the ground speed.
            const double hoverThrust = setup.value(QStringLiteral("hoverThrustG")).toDouble();
            if (haveNow && fromOutputs && !m_livePayloadAboard && packV > 0.0 && hoverThrust > 0.0
                && std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM
                && MotorTable::isCruiseSample(gsNow, climb)) {
                const int n = static_cast<int>(thr.size());
                const double hoverW = n * MotorTable::powerForThrustAt(*table, hoverThrust, sigma).watts;
                if (hoverW > 0.0) {
                    const double ratio = (now.amps - MotorTable::kElectronicsA) * packV / hoverW;
                    m_cruiseRatioSum += ratio;
                    ++m_cruiseSamples;
                    QPair<double, int> &band = m_cruiseBands[MotorTable::cruiseBand(gsNow)];
                    band.first += ratio;
                    ++band.second;
                    m_tableLog.cruiseS += 2.0;
                }
            }
            // The fine-tuning from the charger comparison: the table's count
            // and current × the factor (a current sensor's count is not).
            const double calFactor = setup.value(QStringLiteral("calibrationFactor"), 1.0).toDouble();
            in.factor = gcsCount ? calFactor : 1.0;
            in.currentA = -1.0;
            in.throttleRatio = 1.0;
            if (gcsCount)
                in.fcMahUsed = !armed ? 0.0 : m_gcsCountKnown ? m_gcsUsedAh * 1000.0 : -1.0;
            else
                in.fcMahUsed = !armed ? 0.0
                             : (m_gcsCountKnown && fcMahUsed >= 0.0 && m_fcMahAtArm >= 0.0)
                                   ? std::max(0.0, fcMahUsed - m_fcMahAtArm) : -1.0;

            // The motors' power now: from the table, or from a current sensor
            // (its current less the electronics, at the pack voltage).
            const double vNow = packV > 0.0 ? packV : table->volts;
            // From the motor outputs: the average of the readings since the
            // last update.
            const double wNow = !haveNow ? -1.0
                              : (!gcsCount && m_liveSmoothAmps > 0.0)
                                    ? std::max(0.0, m_liveSmoothAmps - MotorTable::kElectronicsA) * vNow
                              : readingsCounted ? m_readingWattsSum / m_readings
                                                : (now.amps - MotorTable::kElectronicsA) * vNow;
            m_readingWattsSum = 0.0;
            m_readings = 0;
            if (haveNow) {
                // Smoothed like the FC current: α 0.095 per 2 s update, about 20 s.
                m_tableSmoothA = m_tableSmoothA <= 0.0 ? now.amps : 0.095 * now.amps + 0.905 * m_tableSmoothA;
                m_tableSmoothW = m_tableSmoothW <= 0.0 ? wNow : 0.095 * wNow + 0.905 * m_tableSmoothW;
            }
            // The pack's charge left now, and its resistance seen now
            // (resting voltage at this charge − the voltage under load, over
            // the current).
            const MotorTable::Pack pack = motorTablePack(setup.value(QStringLiteral("cells")).toInt() > 0
                                                             ? setup.value(QStringLiteral("cells")).toInt() : table->cells);
            const double capAh = capacityAh();
            const double socNow = (in.fcMahUsed >= 0.0 && capAh > 0.0)
                ? (capAh * std::clamp(m_armStartSocPct, 0.0, 100.0) / 100.0 - in.fcMahUsed / 1000.0 * in.factor) / capAh * 100.0
                : -1.0;
            if (haveNow && packV > 0.0 && socNow > 0.0) {
                const double r = MotorTable::observedResistance(pack, socNow, packV, now.amps);
                if (r >= 0.0) {
                    m_packOhmLive = m_packOhmLive < 0.0 ? r : 0.095 * r + 0.905 * m_packOhmLive;
                    ++m_packOhmSamples;
                }
            }
            // Time left and the trip: the motors' power now, drawn through the
            // rest of the pack down to the reserve. The pack's voltage keeps
            // falling, so the same power needs more current later; costing it
            // at today's current would overstate the time left. Hover and
            // cruise alike, the payload already in the throttle.
            const double socReserve = m_batteryReserve * 100.0;
            double currentNow = -1.0;
            if (armed && m_tableSmoothW > 0.0 && socNow > socReserve)
                currentNow = MotorTable::averageCurrent(pack, m_tableSmoothW, socNow, socReserve);
            if (armed && currentNow <= 0.0)
                currentNow = (!gcsCount && m_liveSmoothAmps > 0.0) ? m_liveSmoothAmps : m_tableSmoothA;
            if (gcsCount && currentNow > 0.0)
                currentNow *= calFactor;
            if (currentNow > 0.0) {
                in.model.hover0 = currentNow;
                in.model.cruise0 = currentNow;
                in.model.status = PowerTypeModel::FitStatus::PayloadNotModelled;
            } else if (!armed && setup.value(QStringLiteral("hoverA")).toDouble() > 0.0) {
                // On the ground: the remembered hover through the rest of the
                // pack, the entered payload added; cruise taken at hover.
                in.model.hover0 = setup.value(QStringLiteral("hoverA")).toDouble();
                in.model.hoverK = setup.value(QStringLiteral("hoverAPerKg")).toDouble();
                in.model.cruise0 = setup.value(QStringLiteral("cruiseA"), in.model.hover0).toDouble();
                in.model.cruiseK = setup.value(QStringLiteral("cruiseAPerKg"), in.model.hoverK).toDouble();
                in.model.status = PowerTypeModel::FitStatus::PayloadNotModelled;
            }
            out[QStringLiteral("motorPowerW")] = haveNow ? m_tableSmoothW : -1.0;
            out[QStringLiteral("socNowPct")] = socNow;
            out[QStringLiteral("packOhm")] = pack.resistanceOhm;
            out[QStringLiteral("packOhmLive")] = m_packOhmLive;
            if (haveNow && m_tableSmoothW > 0.0)
                out[QStringLiteral("reserveVolts")] = MotorTable::loadedVolts(pack, socReserve, m_tableSmoothW);
            out[QStringLiteral("chargeSource")] = !gcsCount ? QStringLiteral("fc")
                                                : in.fcMahUsed >= 0.0 ? QStringLiteral("gcs") : QStringLiteral("fcPercent");
            out[QStringLiteral("tableCurrentA")] = haveNow ? now.amps : -1.0;
            out[QStringLiteral("motorsBeyond")] = haveNow ? now.motorsBeyond : 0;
            out[QStringLiteral("hoverSource")] = setup.value(QStringLiteral("hoverSource"));
            out[QStringLiteral("densityRatio")] = sigma;
            out[QStringLiteral("densitySource")] = setup.value(QStringLiteral("densitySource"));
            out[QStringLiteral("throttleSource")] = fromOutputs ? QStringLiteral("motor outputs")
                                                  : thr.isEmpty() ? QStringLiteral("none") : QStringLiteral("overall throttle");
        } else {
            // Where the charge used comes from. An FC current that follows
            // throttle (a current sensor): its mAh × the charger factor, and its
            // live current corrects the model. None (BATT_MONITOR 0 or 3), or
            // ArduPilot's synthetic current, which on a copter stays at its idle
            // offset (BATT_MONITOR 25, or a recorded current that is the same in
            // hover and cruise): the GCS counts it itself, the model current ×
            // (throttle now ÷ the phase's learned throttle)^1.5, from the arm.
            const bool gcsCount = monitor == 0 || monitor == 3 || monitor == 25
                                  || !m_powerSummary.value(QStringLiteral("fcCurrentFollowsThrottle"), true).toBool();
            const double gs = std::isfinite(in.groundSpeedMs) ? in.groundSpeedMs : 0.0;
            const bool hover = PowerTypeModel::isHover(gs);
            const QVariant thr = v->throttlePct()->rawValue();
            const double throttle = thr.isValid() ? thr.toDouble() / 100.0 : -1.0;
            const double refThrottle = m_powerSummary.value(hover ? QStringLiteral("refThrottleHover")
                                                                  : QStringLiteral("refThrottleCruise"), -1.0).toDouble();
            const double throttleRatio = PowerTypeModel::throttleRatio(throttle, refThrottle);
            if (gcsCount) {
                if (m_gcsCounting && m_gcsLastMs > 0) {
                    const double alt = m_telemetry->altitudeRelative();
                    const bool airborne = !hover || (std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM);
                    // On the ground armed the motors idle: count the hover current
                    // only once airborne, the way the recorder does.
                    if (airborne)
                        m_gcsUsedAh += PowerTypeModel::chargeStepAh(in.model, hover,
                                                                    livePayloadAboardKg(),
                                                                    throttleRatio, (nowMs - m_gcsLastMs) / 1000.0);
                }
                m_gcsLastMs = nowMs;
                in.factor = 1.0;
                in.currentA = -1.0;
                in.throttleRatio = throttleRatio;
                // On the ground the resting voltage already gives the charge (the
                // count of a finished flight is in it); in flight the count since
                // the arm comes off the charge measured just before it. A flight
                // the GCS joined armed falls back to the FC's charge %.
                in.fcMahUsed = !armed ? 0.0 : m_gcsCountKnown ? m_gcsUsedAh * 1000.0 : -1.0;
            } else {
                in.factor = m_powerSummary.value(QStringLiteral("chargerFactor"), 1.0).toDouble();
                // The FC counts from power-up: in flight, what it counted since
                // the arm; on the ground, nothing (the voltage gives the charge).
                in.fcMahUsed = !armed ? 0.0
                             : (m_gcsCountKnown && fcMahUsed >= 0.0 && m_fcMahAtArm >= 0.0)
                                   ? std::max(0.0, fcMahUsed - m_fcMahAtArm) : -1.0;
                in.currentA = m_liveSmoothAmps > 0.0 ? m_liveSmoothAmps : -1.0;
            }
            out[QStringLiteral("chargeSource")] = !gcsCount ? QStringLiteral("fc")
                                                : in.fcMahUsed >= 0.0 ? QStringLiteral("gcs") : QStringLiteral("fcPercent");
        }
        in.altitudeM = m_telemetry->altitudeRelative();
        if (pos.isValid()) {
            in.lat = pos.latitude();
            in.lon = pos.longitude();
        }
        const QGeoCoordinate home = v->homePosition();
        if (home.isValid()) {
            in.homeLat = home.latitude();
            in.homeLon = home.longitude();
        }
        if (!target.isEmpty()) {
            in.targetLat = target.value(QStringLiteral("lat")).toDouble();
            in.targetLon = target.value(QStringLiteral("lon")).toDouble();
        }
        // Before the arm the payload entered is what will be aboard.
        in.payloadAboard = armed ? m_livePayloadAboard : (m_payloadEntered && m_payloadWeightKg > 0.0);
        // After a drop only what is still aboard is carried.
        in.payloadKg = armed ? livePayloadAboardKg() : m_payloadWeightKg;
        in.payloadAboard = in.payloadAboard && in.payloadKg > 0.0;
        in.minAtTarget = m_timeAtTargetMin >= 0.0 ? m_timeAtTargetMin : 0.0;
        in.cruiseSpeedMs = table ? setup.value(QStringLiteral("cruiseSpeedMs"), -1.0).toDouble() : m_powerCruiseSpeedMs;
        in.landMin = landMin();
        in.armed = armed;
        const PowerTypeModel::LiveResult r = PowerTypeModel::liveEstimate(in);
        out[QStringLiteral("active")] = r.valid;
        out[QStringLiteral("armed")] = armed;
        out[QStringLiteral("airborne")] = r.airborne;
        out[QStringLiteral("hover")] = r.hover;
        out[QStringLiteral("usableAh")] = r.usableAh;
        out[QStringLiteral("currentA")] = r.currentA;
        out[QStringLiteral("timeLeftMin")] = r.timeLeftMin;
        out[QStringLiteral("distLeftKm")] = r.distLeftKm;
        out[QStringLiteral("hoverMin")] = r.hoverMin;
        out[QStringLiteral("cruiseMin")] = r.cruiseMin;
        out[QStringLiteral("cruiseKm")] = r.cruiseKm;
        out[QStringLiteral("tripKnown")] = r.tripKnown;
        out[QStringLiteral("tripNeedAh")] = r.tripNeedAh;
        out[QStringLiteral("tripShortAh")] = r.tripShortAh;
        out[QStringLiteral("returnNow")] = r.returnNow;
        out[QStringLiteral("payloadAboard")] = in.payloadAboard;
        out[QStringLiteral("payloadAboardKg")] = in.payloadAboard ? in.payloadKg : 0.0;
        out[QStringLiteral("payloadsReleased")] = m_drop.released();
        out[QStringLiteral("payloadCount")] = dropConfig().payloads;
        out[QStringLiteral("dropOutputSet")] = dropConfig().isSet();
        out[QStringLiteral("startSocPct")] = in.startSocPct;
        out[QStringLiteral("source")] = table ? QStringLiteral("motor table") : m_powerSummary.value(QStringLiteral("source"));
        out[QStringLiteral("typeName")] = table ? table->name : m_powerSummary.value(QStringLiteral("name"));
        out[QStringLiteral("tableId")] = table ? table->id : QString();
    } else {
        out[QStringLiteral("active")] = false;
    }
    // In the log for after-flight review: when the estimate comes on, and
    // each time the reason it is off changes (the reason is also kept in the
    // map, so the next update can tell whether it changed).
    const bool isActive = out.value(QStringLiteral("active")).toBool();
    if (!isActive)
        out[QStringLiteral("offReason")] =
            !v ? QStringLiteral("no vehicle")
            : m_kind == VehicleKind::Unknown ? QStringLiteral("vehicle type not known yet")
            : m_kind != VehicleKind::Multirotor ? QStringLiteral("not a copter")
            : table ? (batteryPackWaived() ? QStringLiteral("pack waived")
                       : setup.value(QStringLiteral("off")).toBool()
                             ? QStringLiteral("motor table: ") + [&setup]() {
                                   for (const QVariant &i : setup.value(QStringLiteral("issues")).toList())
                                       if (i.toMap().value(QStringLiteral("level")).toString() == QLatin1String("off"))
                                           return i.toMap().value(QStringLiteral("text")).toString();
                                   return QString();
                               }()
                       : capacityAh() <= 0.0 ? QStringLiteral("no pack capacity")
                       : !armed ? QStringLiteral("motor table: hover not known yet, fly once")
                       : QStringLiteral("motor table: no motor throttle or charge reading"))
            : m_powerTypeId <= 0 ? QStringLiteral("no power type linked")
            : !m_powerTypeWaivedBy.isEmpty() ? QStringLiteral("power type waived")
            : batteryPackWaived() ? QStringLiteral("pack waived")
            : m_powerSummary.value(QStringLiteral("source")).toString() == QLatin1String("none")
                  ? QStringLiteral("no learned model") : QStringLiteral("no pack capacity or charge reading");
    const bool wasActive = m_powerLive.value(QStringLiteral("active")).toBool();
    const QString why = out.value(QStringLiteral("offReason")).toString();
    if (wasActive != isActive || m_powerLive.isEmpty()
        || (!isActive && why != m_powerLive.value(QStringLiteral("offReason")).toString())) {
        qInfo().noquote() << QStringLiteral("VehicleProfileManager: power-type estimate %1")
                                 .arg(isActive ? QStringLiteral("on (%1, charge used from %2)")
                                                     .arg(out.value(QStringLiteral("typeName")).toString(),
                                                          out.value(QStringLiteral("chargeSource")).toString())
                                               : QStringLiteral("off: ") + why);
    }
    if (m_powerLive.value(QStringLiteral("returnNow")).toBool() != out.value(QStringLiteral("returnNow")).toBool())
        qInfo().noquote() << QStringLiteral("VehicleProfileManager: RETURN NOW %1 (trip needs %2 Ah, %3 Ah left)")
                                 .arg(out.value(QStringLiteral("returnNow")).toBool() ? QStringLiteral("on") : QStringLiteral("off"))
                                 .arg(out.value(QStringLiteral("tripNeedAh")).toDouble(), 0, 'f', 2)
                                 .arg(out.value(QStringLiteral("usableAh")).toDouble(), 0, 'f', 2);
    if (out != m_powerLive) {
        m_powerLive = out;
        emit liveDataChanged();
    }
}

// Full energy of the configured pack: cells in series × nominal cell voltage
// for its chemistry × Ah. It used to guess the cell count from the voltage
// (round(V / 4.2)), which only works on a fully charged pack: a 6S pack at a
// normal 22 V came out as 5S.
double VehicleProfileManager::batteryWh() const
{
    if (m_batterySeriesCells <= 0 || capacityAh() <= 0.0)
        return 0.0;
    return capacityAh() * m_batterySeriesCells * PowerModel::nominalCellVolts(m_batteryType);
}

// ── Motor thrust/current datasheet ──────────────────────────────────────────

void VehicleProfileManager::_applyThrustTable(const QVector<QPair<double, double>> &table,
                                              const QString &info, const QString &warning)
{
    m_thrustTable = table;
    m_thrustTableInfo = info;
    m_thrustTableWarning = warning;
    emit thrustTableChanged();
}

void VehicleProfileManager::_applyDefaultThrustTable()
{
    _applyThrustTable(kDefaultThrustTable,
                      QStringLiteral("built-in default table"), QString());
}

void VehicleProfileManager::_loadPersistedThrustTable(const QString &deviceUid)
{
    const QString json = DatabaseManager::instance().vehicleMotorThrustTable(deviceUid);
    if (json.isEmpty()) {
        _applyDefaultThrustTable();
        return;
    }
    // Re-install the persisted datasheet without re-filtering: the stored JSON
    // was normalized at load time (single prop, single voltage, sorted points).
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "VehicleProfileManager: corrupt persisted thrust table:" << err.errorString();
        _applyDefaultThrustTable();
        return;
    }
    const QJsonObject root = doc.object();
    QVector<QPair<double, double>> table;
    const QJsonArray pts = root.value(QStringLiteral("points")).toArray();
    for (const auto &v : pts) {
        const QJsonObject p = v.toObject();
        const double t = p.value(QStringLiteral("thrust_g")).toDouble();
        const double c = p.value(QStringLiteral("current_a")).toDouble();
        if (t > 0 && c > 0) table.append({t, c});
    }
    if (table.isEmpty()) {
        _applyDefaultThrustTable();
        return;
    }
    std::sort(table.begin(), table.end());
    _applyThrustTable(table,
                      root.value(QStringLiteral("motor")).toString()
                          + QStringLiteral(" ") + root.value(QStringLiteral("propeller")).toString()
                          + QStringLiteral(" @") + QString::number(
                              root.value(QStringLiteral("voltage_v")).toDouble(), 'f', 1)
                          + QStringLiteral("V · ")
                          + QString::number(table.size()) + QStringLiteral(" point(s)"),
                      QString());
}

bool VehicleProfileManager::loadMotorDatasheet(const QString &jsonPathOrPayload,
                                               double preferredVoltageV)
{
    m_thrustTableWarning.clear();

    // Accept a file path or an embedded JSON payload.
    QString text = jsonPathOrPayload.trimmed();
    if (!text.startsWith(QLatin1Char('{'))) {
        QFile f(jsonPathOrPayload);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                              QStringLiteral("Cannot read datasheet file"));
            return false;
        }
        text = QString::fromUtf8(f.readAll()).trimmed();
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                          QStringLiteral("Invalid datasheet JSON"));
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonArray rawPoints = root.value(QStringLiteral("points")).toArray();
    if (rawPoints.isEmpty()) {
        _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                          QStringLiteral("Datasheet has no data points"));
        return false;
    }

    // Collect usable points.  Only thrust_g + current_a are mandatory; every
    // other column in a HobbyWing pull-test export is informational here.
    struct DsPoint { double volt = 0; QString prop; double thrust = 0; double current = 0; };
    QList<DsPoint> pts;
    for (const auto &v : rawPoints) {
        const QJsonObject p = v.toObject();
        DsPoint dp;
        dp.volt    = p.value(QStringLiteral("voltage_v")).toDouble();
        dp.prop    = p.value(QStringLiteral("propeller")).toString().trimmed();
        dp.thrust  = p.value(QStringLiteral("thrust_g")).toDouble();
        dp.current = p.value(QStringLiteral("current_a")).toDouble();
        if (dp.thrust > 0 && dp.current > 0)
            pts.append(dp);
    }
    if (pts.isEmpty()) {
        _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                          QStringLiteral("No points with thrust_g and current_a"));
        return false;
    }

    // Propeller filter: when an export mixes several props pick the group with
    // the most samples so the estimate stays self-consistent.
    QString chosenProp;
    int bestCount = 0;
    QMap<QString, int> propCounts;
    for (const auto &dp : pts)
        if (!dp.prop.isEmpty()) propCounts[dp.prop]++;
    for (auto it = propCounts.constBegin(); it != propCounts.constEnd(); ++it) {
        if (it.value() > bestCount) { bestCount = it.value(); chosenProp = it.key(); }
    }

    // Voltage filter: prefer the configured pack voltage exactly; otherwise
    // use the nearest available and tell the operator we had to.
    QString voltageNote;
    double chosenVolt = 0;
    if (preferredVoltageV > 0) {
        bool exact = false;
        double nearestDiff = std::numeric_limits<double>::max();
        QMap<double, int> voltCounts;
        for (const auto &dp : pts) {
            if (!chosenProp.isEmpty() && dp.prop != chosenProp) continue;
            if (qAbs(dp.volt - preferredVoltageV) < 0.05) exact = true;
            nearestDiff = qMin(nearestDiff, qAbs(dp.volt - preferredVoltageV));
            voltCounts[dp.volt]++;
        }
        if (exact) {
            chosenVolt = preferredVoltageV;
        } else if (!voltCounts.isEmpty()) {
            chosenVolt = std::min_element(voltCounts.keyBegin(), voltCounts.keyEnd(),
                [preferredVoltageV](double a, double b) {
                    return qAbs(a - preferredVoltageV) < qAbs(b - preferredVoltageV);
                }).operator*();
            voltageNote = QStringLiteral("nearest voltage %1V used (wanted %2V)")
                              .arg(chosenVolt, 0, 'f', 1).arg(preferredVoltageV, 0, 'f', 1);
        }
    }

    // Keep only the selected prop/voltage subset, sorted ascending by thrust.
    QVector<QPair<double, double>> table;
    for (const auto &dp : pts) {
        if (!chosenProp.isEmpty() && dp.prop != chosenProp) continue;
        if (chosenVolt > 0 && qAbs(dp.volt - chosenVolt) >= 0.05) continue;
        table.append({dp.thrust, dp.current});
    }
    if (table.isEmpty()) {
        _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                          QStringLiteral("Filter left no usable points"));
        return false;
    }
    std::sort(table.begin(), table.end());

    QString info = root.value(QStringLiteral("motor")).toString();
    if (!chosenProp.isEmpty()) info += QStringLiteral(" ") + chosenProp;
    if (chosenVolt > 0) info += QStringLiteral(" @") + QString::number(chosenVolt, 'f', 1)
                              + QStringLiteral("V");
    info += QStringLiteral(" · ") + QString::number(table.size())
          + QStringLiteral(" point(s)");

    _applyThrustTable(table, info, voltageNote);

    // Persist the normalized subset for the connected vehicle so it survives
    // restarts and reconnects without re-loading the original export.
    if (!m_currentDeviceUid.isEmpty()) {
        QJsonArray outPts;
        for (const auto &tc : table) {
            QJsonObject p;
            p.insert(QStringLiteral("thrust_g"), tc.first);
            p.insert(QStringLiteral("current_a"), tc.second);
            outPts.append(p);
        }
        QJsonObject out;
        out.insert(QStringLiteral("motor"),
                   root.value(QStringLiteral("motor")).toString());
        out.insert(QStringLiteral("propeller"), chosenProp);
        out.insert(QStringLiteral("voltage_v"), chosenVolt);
        out.insert(QStringLiteral("source"),
                   root.value(QStringLiteral("source"))
                       .toString(QStringLiteral("Motor Propeller Pull Test Data")));
        out.insert(QStringLiteral("points"), outPts);
        DatabaseManager::instance().updateVehicleMotorThrustTable(
            m_currentDeviceUid,
            QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    }
    return true;
}

void VehicleProfileManager::setThrustTable(const QVariantList &points)
{
    QVector<QPair<double, double>> table;
    for (const QVariant &v : points) {
        const QVariantMap p = v.toMap();
        const double t = p.value(QStringLiteral("thrust_g")).toDouble();
        const double c = p.value(QStringLiteral("current_a")).toDouble();
        if (t > 0 && c > 0) table.append({t, c});
    }
    if (table.isEmpty()) {
        _applyThrustTable(m_thrustTable, m_thrustTableInfo,
                          QStringLiteral("Rejected empty thrust table"));
        return;
    }
    std::sort(table.begin(), table.end());
    _applyThrustTable(table,
                      QStringLiteral("operator-entered · ")
                          + QString::number(table.size()) + QStringLiteral(" point(s)"),
                      QString());

    // Persist in the same shape as a loaded datasheet so one code path reads both.
    if (!m_currentDeviceUid.isEmpty()) {
        QJsonArray outPts;
        for (const auto &tc : table) {
            QJsonObject p;
            p.insert(QStringLiteral("thrust_g"), tc.first);
            p.insert(QStringLiteral("current_a"), tc.second);
            outPts.append(p);
        }
        QJsonObject out;
        out.insert(QStringLiteral("source"), QStringLiteral("manual entry"));
        out.insert(QStringLiteral("points"), outPts);
        DatabaseManager::instance().updateVehicleMotorThrustTable(
            m_currentDeviceUid,
            QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    }
}

// NOTE: this returns per-MOTOR current from the propulsion datasheet only.
// The flight controller / aux ground draw is a separate constant supplied by
// the operator in the estimator UI — ground-idle current is never used as the
// motor current here.
double VehicleProfileManager::currentForThrust(double thrustGrams) const
{
    const auto &table = m_thrustTable;
    if (table.isEmpty()) return 0;
    if (thrustGrams <= table.first().first)  return table.first().second;
    if (thrustGrams >= table.last().first)   return table.last().second;
    for (int i = 1; i < table.size(); ++i) {
        if (thrustGrams <= table[i].first) {
            const double t = (thrustGrams - table[i-1].first)
                           / (table[i].first - table[i-1].first);
            return table[i-1].second
                 + t * (table[i].second - table[i-1].second);
        }
    }
    return table.last().second;
}

double VehicleProfileManager::maxTableThrust() const
{
    return m_thrustTable.isEmpty() ? 0 : m_thrustTable.last().first;
}

double VehicleProfileManager::minTableThrust() const
{
    return m_thrustTable.isEmpty() ? 0 : m_thrustTable.first().first;
}

// Update the operation location name and persist to the active flight session.
void VehicleProfileManager::setLocationName(const QString &name)
{
    if (m_locationName == name) return;
    m_locationName = name;
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionLocation(m_flightSessionId, m_locationName);
    }
    emit locationNameChanged();
}

// Update planned latitude and persist to the active flight session.
void VehicleProfileManager::setPlanLatitude(double lat)
{
    if (qFuzzyCompare(m_planLat, lat)) return;
    m_planLat = lat;
    if (m_flightSessionId > 0) {
        DatabaseManager::instance().updateFlightSessionPlanLocation(m_flightSessionId, m_planLat, m_planLon);
    }
    emit planLatitudeChanged();
}

// Update planned longitude and persist to the active flight session.
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
/// Tracks the armed timer and the battery % at arm. Battery cycles, energy and
/// distance per flight are no longer written here: SessionRecorder is their one
/// writer (a second writer here double-counted cycles, and its energy guess
/// read PX4's BAT_CAPACITY, so ArduPilot fell back to 0.45 Ah per %).
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
        m_armBatteryPct = -1.0;
    }
}



// ── Power types ──────────────────────────────────────────────────────────
// Rows come from DatabaseManager; the model is fitted on demand from the
// flights the pilot marked "used" (each has a charger number). Until there
// are enough of them, the FC's synthetic current per phase × the charger
// factor gives a starting model, labelled "synthetic".

namespace {

PowerTypeModel::Flight toModelFlight(const QVariantMap &row)
{
    PowerTypeModel::Flight f;
    f.hoverMinEmpty   = std::max(0.0, row.value(QStringLiteral("hoverMinEmpty")).toDouble());
    f.hoverMinLoaded  = std::max(0.0, row.value(QStringLiteral("hoverMinLoaded")).toDouble());
    f.cruiseMinEmpty  = std::max(0.0, row.value(QStringLiteral("cruiseMinEmpty")).toDouble());
    f.cruiseMinLoaded = std::max(0.0, row.value(QStringLiteral("cruiseMinLoaded")).toDouble());
    f.payloadKg       = std::max(0.0, row.value(QStringLiteral("payloadKg")).toDouble());
    const double charger = row.value(QStringLiteral("chargerMah")).toDouble();
    const double fc      = row.value(QStringLiteral("fcMah")).toDouble();
    f.chargerAh = charger > 0.0 ? charger / 1000.0 : -1.0;
    f.fcAh      = fc > 0.0 ? fc / 1000.0 : -1.0;
    return f;
}

bool isUsed(const QVariantMap &row)
{
    return row.value(QStringLiteral("learningState")).toString() == QLatin1String("used")
        && row.value(QStringLiteral("chargerMah")).toDouble() > 0.0;
}

QString statusName(PowerTypeModel::FitStatus s)
{
    switch (s) {
    case PowerTypeModel::FitStatus::TooAlike:            return QStringLiteral("tooAlike");
    case PowerTypeModel::FitStatus::PayloadNotModelled:  return QStringLiteral("payloadNotModelled");
    case PowerTypeModel::FitStatus::PayloadProportional: return QStringLiteral("payloadProportional");
    case PowerTypeModel::FitStatus::Full:                return QStringLiteral("full");
    case PowerTypeModel::FitStatus::NotEnoughFlights:    break;
    }
    return QStringLiteral("none");
}

struct FittedType {
    PowerTypeModel::Model model;
    QVector<PowerTypeModel::Flight> used;
    bool fromCharger = false;
    double factor = 1.0;
    int pending = 0;
    bool fcCurrentFollowsThrottle = true;   ///< false: the FC current is a constant (synthetic on a copter)
    double refThrottleHover = -1.0;          ///< The used flights' average throttle per phase (0–1)
    double refThrottleCruise = -1.0;
};

FittedType fitType(const QVariantList &rows)
{
    FittedType t;
    double synthHover = 0.0, synthCruise = 0.0;
    int synthN = 0;
    double thrHover = 0.0, thrCruise = 0.0;
    int thrHoverN = 0, thrCruiseN = 0;
    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        if (isUsed(row)) {
            t.used << toModelFlight(row);
            const double th = row.value(QStringLiteral("refThrottleHover")).toDouble();
            const double tc = row.value(QStringLiteral("refThrottleCruise")).toDouble();
            if (th > 0.0) { thrHover += th; ++thrHoverN; }
            if (tc > 0.0) { thrCruise += tc; ++thrCruiseN; }
        }
        const QString state = row.value(QStringLiteral("learningState")).toString();
        if (state == QLatin1String("pending"))
            ++t.pending;
        const double h = row.value(QStringLiteral("synthHoverA")).toDouble();
        const double c = row.value(QStringLiteral("synthCruiseA")).toDouble();
        if (state != QLatin1String("excluded") && h > 0.0 && c > 0.0) {
            synthHover += h; synthCruise += c; ++synthN;
        }
    }
    if (thrHoverN > 0) t.refThrottleHover = thrHover / thrHoverN;
    if (thrCruiseN > 0) t.refThrottleCruise = thrCruise / thrCruiseN;
    if (synthN > 0)
        t.fcCurrentFollowsThrottle = PowerTypeModel::currentFollowsThrottle(synthHover / synthN, synthCruise / synthN);
    t.factor = PowerTypeModel::chargerFactor(t.used);
    t.model = PowerTypeModel::fit(t.used);
    t.fromCharger = t.model.usable();
    // A constant FC current says nothing about hover or cruise: no seed from it.
    if (!t.fromCharger && synthN > 0 && t.fcCurrentFollowsThrottle) {
        const QString advice = t.model.advice;
        t.model = PowerTypeModel::seedFromSynthetic(synthHover / synthN, synthCruise / synthN, t.factor);
        t.model.advice = advice.isEmpty() ? t.model.advice : advice;
    }
    return t;
}

} // namespace

QString VehicleProfileManager::currentPowerTypeName() const
{
    if (m_powerTypeId <= 0) return {};
    return DatabaseManager::instance().powerType(m_powerTypeId).value(QStringLiteral("name")).toString();
}

QVariantList VehicleProfileManager::listPowerTypes()
{
    return DatabaseManager::instance().listPowerTypes();
}

int VehicleProfileManager::savePowerType(int id, const QString &frame, double propDiameterIn,
                                         double propPitchIn, int cells, const QString &motorNote)
{
    const int saved = DatabaseManager::instance().savePowerType(id, frame, propDiameterIn,
                                                                propPitchIn, cells, motorNote);
    if (saved > 0)
        emit powerTypeChanged();
    return saved;
}

bool VehicleProfileManager::deletePowerType(int id)
{
    if (!DatabaseManager::instance().deletePowerType(id))
        return false;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::linkCurrentVehiclePowerType(int powerTypeId)
{
    if (m_currentDeviceUid.isEmpty()
        || !DatabaseManager::instance().linkVehiclePowerType(m_currentDeviceUid, powerTypeId))
        return false;
    const int linked = DatabaseManager::instance().vehiclePowerTypeId(m_currentDeviceUid);
    if (linked != m_powerTypeId)
        m_propsConfirmed = false;   // the confirmation was for the other type's props
    m_powerTypeId = linked;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::linkCurrentVehicleMotorTable(const QString &tableId)
{
    if (m_currentDeviceUid.isEmpty() || (!tableId.isEmpty() && !MotorTable::byId(tableId))
        || !DatabaseManager::instance().linkVehicleMotorTable(m_currentDeviceUid, tableId))
        return false;
    m_motorTableId = DatabaseManager::instance().vehicleMotorTableId(m_currentDeviceUid);
    m_tableSmoothA = 0.0;
    emit powerTypeChanged();
    return true;
}

QVariantList VehicleProfileManager::motorTables() const
{
    QVariantList out;
    for (const MotorTable::Table *t : MotorTable::all()) {
        QVariantMap m;
        m[QStringLiteral("id")] = t->id;
        m[QStringLiteral("name")] = t->name;
        m[QStringLiteral("volts")] = t->volts;
        m[QStringLiteral("motors")] = t->motors;
        m[QStringLiteral("cells")] = t->cells;
        out.append(m);
    }
    return out;
}

QVariantMap VehicleProfileManager::powerTypeSummary(int powerTypeId)
{
    QVariantMap out;
    const QVariantMap type = DatabaseManager::instance().powerType(powerTypeId);
    if (type.isEmpty()) return out;
    const FittedType t = fitType(DatabaseManager::instance().powerTypeFlights(powerTypeId));
    out[QStringLiteral("id")]             = powerTypeId;
    out[QStringLiteral("name")]           = type.value(QStringLiteral("name"));
    out[QStringLiteral("status")]         = statusName(t.model.status);
    out[QStringLiteral("source")]         = t.fromCharger ? QStringLiteral("charger")
                                          : t.model.usable() ? QStringLiteral("synthetic")
                                                             : QStringLiteral("none");
    out[QStringLiteral("hover0")]         = t.model.hover0;
    out[QStringLiteral("hoverK")]         = t.model.hoverK;
    out[QStringLiteral("cruise0")]        = t.model.cruise0;
    out[QStringLiteral("cruiseK")]        = t.model.cruiseK;
    out[QStringLiteral("advice")]         = t.model.advice;
    out[QStringLiteral("validated")]      = t.fromCharger && PowerTypeModel::isValidated(t.used);
    out[QStringLiteral("chargerFactor")]  = t.factor;
    out[QStringLiteral("usedFlights")]    = t.used.size();
    out[QStringLiteral("pendingFlights")] = t.pending;
    out[QStringLiteral("fcCurrentFollowsThrottle")] = t.fcCurrentFollowsThrottle;
    out[QStringLiteral("refThrottleHover")]  = t.refThrottleHover;
    out[QStringLiteral("refThrottleCruise")] = t.refThrottleCruise;
    return out;
}

QVariantList VehicleProfileManager::powerTypeFlights(int powerTypeId)
{
    QVariantList rows = DatabaseManager::instance().powerTypeFlights(powerTypeId);
    const FittedType t = fitType(rows);
    if (!t.fromCharger)
        return rows;
    for (QVariant &v : rows) {
        QVariantMap row = v.toMap();
        const PowerTypeModel::Flight f = toModelFlight(row);
        row[QStringLiteral("predictedMah")] = t.model.predictAh(f) * 1000.0;
        row[QStringLiteral("outlier")]      = PowerTypeModel::isOutlier(t.model, f);
        v = row;
    }
    return rows;
}

bool VehicleProfileManager::setFlightChargerMah(int sessionId, double chargerMah)
{
    if (!DatabaseManager::instance().setFlightChargerMah(sessionId, chargerMah))
        return false;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::setFlightLearningState(int sessionId, const QString &state)
{
    if (!DatabaseManager::instance().setFlightLearningState(sessionId, state))
        return false;
    emit powerTypeChanged();
    return true;
}

// ── Per-flight power-type state and the mission check ────────────────────
// The "Power type" and "Mission check" checklist items read this state; it
// lasts one flight (PreflightPlugin clears it after a non-bench flight and on
// a vehicle change), like the battery-pack waiver.

bool VehicleProfileManager::recordWaiver(const QString &event, const QString &pilot,
                                         const QString &reason, const QString &checkId)
{
    const QString name = pilot.trimmed();
    if (name.isEmpty())
        return false;
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    QJsonObject entry;
    entry[QStringLiteral("event")] = event;
    entry[QStringLiteral("pilot")] = name;
    entry[QStringLiteral("reason")] = reason;
    entry[QStringLiteral("checks")] = QJsonArray{checkId};
    entry[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return DatabaseManager::instance().saveComplianceLog(
        QStringLiteral("gate-%1-%2").arg(event, QUuid::createUuid().toString(QUuid::WithoutBraces)),
        v ? QString::number(v->id()) : QStringLiteral("unknown"),
        v ? v->vehicleTypeString() : QStringLiteral("unknown"), name,
        QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)), QString());
}

bool VehicleProfileManager::confirmPowerTypeProps()
{
    if (m_powerTypeId <= 0)
        return false;
    m_propsConfirmed = true;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::waivePowerType(const QString &pilot)
{
    if (!recordWaiver(QStringLiteral("power_type_waived"), pilot,
                      QStringLiteral("No power type; rough flight-time estimate, no mission check"),
                      QStringLiteral("power.power_type")))
        return false;
    m_powerTypeWaivedBy = pilot.trimmed();
    emit powerTypeChanged();
    return true;
}

void VehicleProfileManager::clearPowerTypeFlightState()
{
    if (!m_propsConfirmed && m_powerTypeWaivedBy.isEmpty())
        return;
    m_propsConfirmed = false;
    m_powerTypeWaivedBy.clear();
    emit powerTypeChanged();
}

double VehicleProfileManager::packRestingSocPct(double packVolts) const
{
    if (!(packVolts > 1.0) || m_batterySeriesCells <= 0)
        return 100.0;   // no reading: nothing to go on, as before
    const double cellV = packVolts / m_batterySeriesCells;
    if (cellV >= PowerTypeModel::fullCellVolts(m_batteryType) - 1e-9)
        return 100.0;
    const double soc = PowerTypeModel::restingSocPct(cellV, m_batteryType);
    return soc >= 0.0 ? std::min(soc, 99.0) : 100.0;
}

QString VehicleProfileManager::frameName(int frameClass, int frameType)
{
    static const QHash<int, QString> classes = {
        {1, QStringLiteral("Quad")}, {2, QStringLiteral("Hexa")}, {3, QStringLiteral("Octa")},
        {4, QStringLiteral("OctaQuad")}, {5, QStringLiteral("Y6")}, {7, QStringLiteral("Tri")},
        {12, QStringLiteral("DodecaHexa")}, {14, QStringLiteral("Deca")}};
    static const QHash<int, QString> types = {
        {0, QStringLiteral("Plus")}, {1, QStringLiteral("X")}, {2, QStringLiteral("V")},
        {3, QStringLiteral("H")}};
    if (!classes.contains(frameClass))
        return {};
    const QString t = types.value(frameType);
    return t.isEmpty() ? classes.value(frameClass) : QStringLiteral("%1 %2").arg(classes.value(frameClass), t);
}

QString VehicleProfileManager::currentFrameName() const
{
    if (!m_telemetry || !m_telemetry->hasParameter(QStringLiteral("FRAME_CLASS")))
        return {};
    return frameName(qRound(m_telemetry->parameterValue(QStringLiteral("FRAME_CLASS"), -1.0f)),
                     qRound(m_telemetry->parameterValue(QStringLiteral("FRAME_TYPE"), -1.0f)));
}

QVariantMap VehicleProfileManager::missionCheck()
{
    QVariantMap out;
    out[QStringLiteral("applicable")] = m_kind == VehicleKind::Multirotor;
    if (m_kind != VehicleKind::Multirotor)
        return out;
    const MotorTable::Table *table = MotorTable::byId(m_motorTableId);
    out[QStringLiteral("linked")] = m_powerTypeId > 0 || table;
    out[QStringLiteral("powerTypeWaivedBy")] = table ? QString() : m_powerTypeWaivedBy;
    out[QStringLiteral("packWaived")] = batteryPackWaived();

    PowerTypeModel::Model model;
    double cruise = 10.0;
    bool speedAssumed = true;
    if (table) {
        // A copter on a motor table: the hover remembered from its last flight
        // (or its learned MOT_THST_HOVER) at the planning voltage, the payload
        // added from the table's thrust column, cruise taken at hover (safe
        // side) until measured, at the copter's mission speed. The table is not proven on this copter until
        // its flights agree with the charger, so a trip that does not fit is
        // a warning, not a block.
        out[QStringLiteral("source")] = QStringLiteral("motor table");
        out[QStringLiteral("typeName")] = table->name;
        out[QStringLiteral("validated")] = false;
        if (batteryPackWaived())
            return out;
        const QVariantMap setup = motorTableSetup();
        // Proven on this copter by its charger numbers: a trip that does not
        // fit may then hold the arm (with a waiver), as for a validated type.
        out[QStringLiteral("validated")] = setup.value(QStringLiteral("proven")).toBool();
        out[QStringLiteral("calibrationFactor")] = setup.value(QStringLiteral("calibrationFactor"));
        out[QStringLiteral("calibrationFlights")] = setup.value(QStringLiteral("calibrationFlights"));
        out[QStringLiteral("hoverSource")] = setup.value(QStringLiteral("hoverSource"));
        out[QStringLiteral("hoverA")] = setup.value(QStringLiteral("hoverA"));
        out[QStringLiteral("weightKg")] = setup.value(QStringLiteral("weightKg"));
        QStringList warns;
        QString offText;
        for (const QVariant &i : setup.value(QStringLiteral("issues")).toList()) {
            const QVariantMap m = i.toMap();
            if (m.value(QStringLiteral("level")).toString() == QLatin1String("off")) {
                if (offText.isEmpty())
                    offText = m.value(QStringLiteral("text")).toString();
            } else {
                warns << m.value(QStringLiteral("text")).toString();
            }
        }
        out[QStringLiteral("setupWarning")] = warns.join(QStringLiteral("; "));
        if (!offText.isEmpty()) {
            out[QStringLiteral("modelUsable")] = false;
            out[QStringLiteral("notReadyText")] = offText;
            return out;
        }
        if (!(setup.value(QStringLiteral("hoverA")).toDouble() > 0.0)) {
            out[QStringLiteral("modelUsable")] = false;
            out[QStringLiteral("notReadyText")] =
                QStringLiteral("Hover not known yet: fly once (30 s of steady hover), or let the copter learn MOT_THST_HOVER");
            return out;
        }
        out[QStringLiteral("modelUsable")] = true;
        model.hover0 = setup.value(QStringLiteral("hoverA")).toDouble();
        model.hoverK = setup.value(QStringLiteral("hoverAPerKg")).toDouble();
        model.cruise0 = setup.value(QStringLiteral("cruiseA"), model.hover0).toDouble();
        model.cruiseK = setup.value(QStringLiteral("cruiseAPerKg"), model.hoverK).toDouble();
        cruise = setup.value(QStringLiteral("cruiseSpeedMs"), -1.0).toDouble();
        speedAssumed = false;
        if (!(cruise > 0.0)) {
            out[QStringLiteral("modelUsable")] = false;
            out[QStringLiteral("notReadyText")] = QStringLiteral("Mission speed not known yet: WP_SPD not read from the copter");
            return out;
        }
        out[QStringLiteral("cruiseMeasured")] = setup.value(QStringLiteral("cruiseMeasured"));
        if (!setup.value(QStringLiteral("cruiseMeasured")).toBool())
            out[QStringLiteral("cruiseNote")] = QStringLiteral("cruise power not measured yet: taken as hover");
    } else {
        if (m_powerTypeId <= 0 || !m_powerTypeWaivedBy.isEmpty() || batteryPackWaived())
            return out;

        const QVariantMap sum = powerTypeSummary(m_powerTypeId);
        out[QStringLiteral("typeName")] = sum.value(QStringLiteral("name"));
        out[QStringLiteral("source")] = sum.value(QStringLiteral("source"));
        out[QStringLiteral("advice")] = sum.value(QStringLiteral("advice"));
        out[QStringLiteral("validated")] = sum.value(QStringLiteral("validated"));
        const bool usable = sum.value(QStringLiteral("source")).toString() != QLatin1String("none");
        out[QStringLiteral("modelUsable")] = usable;
        if (!usable)
            return out;

        model.hover0  = sum.value(QStringLiteral("hover0")).toDouble();
        model.hoverK  = sum.value(QStringLiteral("hoverK")).toDouble();
        model.cruise0 = sum.value(QStringLiteral("cruise0")).toDouble();
        model.cruiseK = sum.value(QStringLiteral("cruiseK")).toDouble();

        // Cruise speed: what the type's used flights flew at (the currents were
        // learned at that speed); 10 m/s, said so, before any flight has one.
        double speedSum = 0.0;
        int speedN = 0;
        for (const QVariant &v : DatabaseManager::instance().powerTypeFlights(m_powerTypeId)) {
            const QVariantMap f = v.toMap();
            const double spd = f.value(QStringLiteral("cruiseSpeedMs")).toDouble();
            if (f.value(QStringLiteral("learningState")).toString() == QLatin1String("used") && spd > 0.0) {
                speedSum += spd;
                ++speedN;
            }
        }
        cruise = speedN > 0 ? speedSum / speedN : 10.0;
        speedAssumed = speedN == 0;
    }
    out[QStringLiteral("cruiseSpeedMs")] = cruise;
    out[QStringLiteral("speedAssumed")] = speedAssumed;

    PowerTypeModel::Mission x;
    x.payloadKg = m_payloadEntered ? m_payloadWeightKg : 0.0;
    x.minAtTarget = m_timeAtTargetMin >= 0.0 ? m_timeAtTargetMin : 0.0;
    x.outSpeedMs = cruise;
    x.backSpeedMs = cruise;
    x.takeoffMin = takeoffMin();
    x.landMin = landMin();
    // The pack's charge from its resting voltage (on the ground the motors
    // are off): 100 % when full, else the discharge-curve estimate. The FC's
    // own % is not used: with no current sensor it does not know the charge.
    const double packVolts = m_telemetry ? m_telemetry->batteryVoltage() : 0.0;
    const double cellV = (packVolts > 1.0 && m_batterySeriesCells > 0) ? packVolts / m_batterySeriesCells : -1.0;
    const double soc = packRestingSocPct(packVolts);
    x.usableAh = usableAhAboveReserveFor(qRound(soc));
    out[QStringLiteral("packCellV")] = cellV;
    out[QStringLiteral("packFull")] = soc >= 100.0;
    out[QStringLiteral("packSocPct")] = soc;
    out[QStringLiteral("payloadEntered")] = m_payloadEntered;
    out[QStringLiteral("payloadKg")] = x.payloadKg;
    out[QStringLiteral("timeAtTargetEntered")] = m_timeAtTargetMin >= 0.0;
    out[QStringLiteral("minAtTarget")] = x.minAtTarget;
    out[QStringLiteral("usableAh")] = x.usableAh;

    // Distance from where the copter is now (on the ground: home) to the
    // target saved on the gimbal page.
    const QVariantMap target = DatabaseManager::instance().vehicleTarget(m_currentDeviceUid);
    const double lat = m_telemetry ? m_telemetry->gpsLatitude() : 0.0;
    const double lon = m_telemetry ? m_telemetry->gpsLongitude() : 0.0;
    const bool positionKnown = std::abs(lat) > 1e-6 || std::abs(lon) > 1e-6;
    out[QStringLiteral("hasTarget")] = !target.isEmpty();
    out[QStringLiteral("positionKnown")] = positionKnown;
    if (!target.isEmpty() && positionKnown)
        x.targetKm = PowerTypeModel::distanceM(lat, lon, target.value(QStringLiteral("lat")).toDouble(),
                                               target.value(QStringLiteral("lon")).toDouble()) / 1000.0;
    out[QStringLiteral("targetKm")] = x.targetKm;

    const PowerTypeModel::MissionResult r = PowerTypeModel::checkMission(model, x);
    out[QStringLiteral("takeoffAh")] = r.takeoffAh;
    out[QStringLiteral("outAh")] = r.outAh;
    out[QStringLiteral("targetAh")] = r.targetAh;
    out[QStringLiteral("backAh")] = r.backAh;
    out[QStringLiteral("landAh")] = r.landAh;
    out[QStringLiteral("totalAh")] = r.totalAh;
    out[QStringLiteral("totalMin")] = r.totalMin;
    out[QStringLiteral("spareAh")] = r.spareAh;
    out[QStringLiteral("maxTargetKm")] = r.maxTargetKm;
    out[QStringLiteral("fits")] = r.fits;
    return out;
}

// ── Fleet settings and the BATT_AMP_PERVLT suggestion ────────────────────

namespace {
double settingMinutes(const char *key)
{
    QSettings settings;
    return std::clamp(settings.value(QStringLiteral("PowerTypes/%1").arg(QLatin1String(key)), 1.0).toDouble(),
                      0.0, 10.0);
}
bool storeMinutes(const char *key, double minutes)
{
    if (!std::isfinite(minutes) || minutes < 0.0 || minutes > 10.0)
        return false;
    QSettings settings;
    settings.setValue(QStringLiteral("PowerTypes/%1").arg(QLatin1String(key)), minutes);
    return true;
}
} // namespace

double VehicleProfileManager::takeoffMin() const { return settingMinutes("takeoffMin"); }
double VehicleProfileManager::landMin() const { return settingMinutes("landMin"); }

bool VehicleProfileManager::setTakeoffMin(double minutes)
{
    if (!storeMinutes("takeoffMin", minutes))
        return false;
    emit powerTypeChanged();
    return true;
}

bool VehicleProfileManager::setLandMin(double minutes)
{
    if (!storeMinutes("landMin", minutes))
        return false;
    emit powerTypeChanged();
    return true;
}

QVariantMap VehicleProfileManager::ampPerVoltSuggestion()
{
    QVariantMap out;
    out[QStringLiteral("available")] = false;
    if (!m_telemetry || m_currentDeviceUid.isEmpty() || m_powerTypeId <= 0)
        return out;
    const int monitor = m_telemetry->hasParameter(QStringLiteral("BATT_MONITOR"))
        ? qRound(m_telemetry->parameterValue(QStringLiteral("BATT_MONITOR"), -1.0f)) : -1;
    out[QStringLiteral("monitor")] = monitor;
    // On a copter ArduPilot's synthetic current does not use BATT_AMP_PERVLT
    // in any way that follows throttle (it reads the k_throttle output, which
    // a multicopter does not drive): changing it would change nothing.
    if (m_kind == VehicleKind::Multirotor
        && (monitor == 25
            || !powerTypeSummary(m_powerTypeId).value(QStringLiteral("fcCurrentFollowsThrottle"), true).toBool())) {
        out[QStringLiteral("reason")] = QStringLiteral("not used on a copter: the GCS counts the charge itself");
        return out;
    }
    if (!m_telemetry->hasParameter(QStringLiteral("BATT_AMP_PERVLT")))
        return out;
    const double current = m_telemetry->parameterValue(QStringLiteral("BATT_AMP_PERVLT"), 0.0f);
    out[QStringLiteral("ampPerVolt")] = current;

    // This copter's latest flight with both numbers (flights come oldest first).
    const QVariantList rows = DatabaseManager::instance().powerTypeFlights(m_powerTypeId);
    for (int i = rows.size() - 1; i >= 0; --i) {
        const QVariantMap f = rows[i].toMap();
        const double charger = f.value(QStringLiteral("chargerMah")).toDouble();
        const double fc = f.value(QStringLiteral("fcMah")).toDouble();
        if (f.value(QStringLiteral("deviceUid")).toString() != m_currentDeviceUid || charger <= 0.0 || fc <= 0.0)
            continue;
        out[QStringLiteral("available")] = current > 0.0;
        out[QStringLiteral("suggested")] = PowerTypeModel::suggestedAmpPerVolt(current, charger, fc);
        out[QStringLiteral("ratio")] = charger / fc;
        out[QStringLiteral("sessionId")] = f.value(QStringLiteral("sessionId"));
        out[QStringLiteral("startedAt")] = f.value(QStringLiteral("startedAt"));
        out[QStringLiteral("chargerMah")] = charger;
        out[QStringLiteral("fcMah")] = fc;
        break;
    }
    return out;
}

// ── Payload release ─────────────────────────────────────────────────
// The release servo output (core/PayloadRelease.h) or the pilot's tap. Each
// release takes one payload's weight off the live estimate and is written on
// the flight record as PAYLOAD_RELEASED.

// A copter parameter from QGroundControl's parameter list (the bridge keeps
// only the watched ones); NaN when it is not there.
double VehicleProfileManager::copterParam(const QString &name) const
{
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    ParameterManager *pm = v ? v->parameterManager() : nullptr;
    if (!pm || !pm->parametersReady() || !pm->parameterExists(v->defaultComponentId(), name))
        return qQNaN();
    Fact *f = pm->getParameter(v->defaultComponentId(), name);
    return f ? f->rawValue().toDouble() : qQNaN();
}

PayloadRelease::Config VehicleProfileManager::dropConfig() const
{
    PayloadRelease::Config c;
    auto *settings = PreflightSettingsManager::instance();
    if (settings) {
        c.output = settings->payloadDropOutput();
        c.onWhenLow = settings->payloadDropOnWhenLow();
        c.payloads = settings->payloadCount();
    }
    c.totalKg = (m_payloadEntered && m_payloadWeightKg > 0.0) ? m_payloadWeightKg : 0.0;
    // "Released" is past the middle of the output's range on the copter, the
    // same value the settings page shows.
    if (c.output > 0)
        c.thresholdUs = servoOutputInfo(c.output).value(QStringLiteral("thresholdUs"),
                                                        PayloadRelease::kDefaultMidUs).toDouble();
    return c;
}

QVariantMap VehicleProfileManager::servoOutputInfo(int output) const
{
    QVariantMap m;
    m[QStringLiteral("known")] = false;
    if (output <= 0) {
        m[QStringLiteral("text")] = QStringLiteral("Not set: no release is detected; use the \"Payload dropped\" button");
        return m;
    }
    const double lo = copterParam(QStringLiteral("SERVO%1_MIN").arg(output));
    const double hi = copterParam(QStringLiteral("SERVO%1_MAX").arg(output));
    const double mid = PayloadRelease::threshold(lo, hi);
    m[QStringLiteral("thresholdUs")] = mid;
    const bool onWhenLow = PreflightSettingsManager::instance() && PreflightSettingsManager::instance()->payloadDropOnWhenLow();
    const QString side = onWhenLow ? QStringLiteral("below") : QStringLiteral("above");
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) {
        m[QStringLiteral("text")] = QStringLiteral("SERVO%1_MIN / SERVO%1_MAX not read from the copter yet: released %2 %3 us")
                                        .arg(output).arg(side).arg(mid, 0, 'f', 0);
        return m;
    }
    m[QStringLiteral("known")] = true;
    m[QStringLiteral("minUs")] = lo;
    m[QStringLiteral("maxUs")] = hi;
    m[QStringLiteral("text")] = QStringLiteral("Servo output %1: %2-%3 us on the copter; released %4 %5 us")
                                    .arg(output).arg(lo, 0, 'f', 0).arg(hi, 0, 'f', 0).arg(side).arg(mid, 0, 'f', 0);
    return m;
}

double VehicleProfileManager::livePayloadAboardKg() const
{
    if (!m_livePayloadAboard)
        return 0.0;
    return std::max(0.0, m_payloadWeightKg - m_drop.releasedKg(dropConfig()));
}

void VehicleProfileManager::_onOutputsForDrop()
{
    if (!m_telemetry)
        return;
    const PayloadRelease::Config c = dropConfig();
    const QVariantList out = m_telemetry->motorOutputs();
    const double pwm = (c.output > 0 && c.output <= out.size()) ? out.at(c.output - 1).toDouble() : qQNaN();
    const bool armed = m_telemetry->armed();
    const double alt = m_telemetry->altitudeRelative();
    const bool airborne = armed && std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM;
    switch (m_drop.feed(c, QDateTime::currentMSecsSinceEpoch(), pwm, armed, airborne)) {
    case PayloadRelease::Seen::Released: {
        const int n = m_drop.released();
        const QString detail = QStringLiteral("Payload %1 of %2 released (%3 kg), servo output %4 at %5 us")
                                   .arg(n).arg(c.payloads).arg(c.eachKg(), 0, 'f', 1).arg(c.output).arg(pwm, 0, 'f', 0);
        _recordDrop(QStringLiteral("PAYLOAD_RELEASED"), detail);
        emit payloadReleased(n, c.eachKg(), QStringLiteral("output"));
        emit liveDataChanged();
        break;
    }
    case PayloadRelease::Seen::OnAtArm: {
        const QString what = QStringLiteral("Release servo (output %1) is at \"released\" at arming: a drop needs it to move there in flight")
                                 .arg(c.output);
        _recordDrop(QStringLiteral("PAYLOAD_RELEASE_ON_AT_ARM"), what);
        emit payloadSwitchWarning(what);
        break;
    }
    case PayloadRelease::Seen::OnOnGround: {
        const QString what = QStringLiteral("Release servo (output %1) moved to \"released\" on the ground: not counted as a drop")
                                 .arg(c.output);
        _recordDrop(QStringLiteral("PAYLOAD_RELEASED_ON_GROUND"), what);
        emit payloadSwitchWarning(what);
        break;
    }
    case PayloadRelease::Seen::Nothing:
        break;
    }
}
bool VehicleProfileManager::payloadDroppedTap()
{
    const PayloadRelease::Config c = dropConfig();
    if (!m_livePayloadAboard || !m_drop.tap(c))
        return false;
    const int n = m_drop.released();
    _recordDrop(QStringLiteral("PAYLOAD_RELEASED"),
                QStringLiteral("Payload %1 of %2 released (%3 kg), tapped by the pilot").arg(n).arg(c.payloads)
                    .arg(c.eachKg(), 0, 'f', 1));
    emit payloadReleased(n, c.eachKg(), QStringLiteral("tap"));
    emit liveDataChanged();
    return true;
}

// On the flight's event list (the Flight History row the recorder has open,
// where its ARM and DISARM go; else the session's flight), with where the
// copter was and the battery.
void VehicleProfileManager::_recordDrop(const QString &eventType, const QString &detail)
{
    auto *session = FlightSession::instance();
    int flightId = DatabaseManager::instance().recordingHistoryFlightId();
    if (flightId <= 0)
        flightId = session ? session->currentFlightId() : -1;
    if (flightId <= 0 || !m_telemetry)
        return;
    Vehicle *v = m_telemetry->vehicle();
    const QGeoCoordinate pos = v ? v->coordinate() : QGeoCoordinate();
    DatabaseManager::instance().insertTelemetryEventSnapshot(
        flightId, eventType, detail, m_telemetry->batteryVoltage(), m_telemetry->altitudeRelative(),
        m_telemetry->gpsSatellites(), m_telemetry->flightMode(), pos.isValid() ? pos.latitude() : 0.0,
        pos.isValid() ? pos.longitude() : 0.0, m_telemetry->gpsHdop(), 0.0, m_telemetry->heading());
}

// ── Mission estimate ────────────────────────────────────────────────
// The waypoint times, the charge on arrival and the assessment for the
// mission on the copter, from the motor table, the copter's parameters, the
// operator's weights and drop point, and the site weather. Advice only.

double VehicleProfileManager::emptyWeightKg() const
{
    if (m_currentDeviceUid.isEmpty())
        return -1.0;
    return QSettings().value(QStringLiteral("MissionEstimate/emptyKg/%1").arg(m_currentDeviceUid), -1.0).toDouble();
}

bool VehicleProfileManager::setEmptyWeightKg(double kg)
{
    if (m_currentDeviceUid.isEmpty() || !std::isfinite(kg) || kg <= 0.0 || kg > 200.0)
        return false;
    QSettings().setValue(QStringLiteral("MissionEstimate/emptyKg/%1").arg(m_currentDeviceUid), kg);
    emit emptyWeightChanged();
    return true;
}

static QString clockText(double s)
{
    if (!std::isfinite(s) || s < 0.0)
        return QStringLiteral("--");
    const int t = qRound(s);
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

static QString commandText(int command, int seq)
{
    if (seq < 0)
        return QStringLiteral("Home (assumed RTL)");
    switch (command) {
    case MissionWalk::NavTakeoff: return QStringLiteral("Take-off");
    case MissionWalk::NavWaypoint: return QStringLiteral("Waypoint");
    case MissionWalk::NavSplineWaypoint: return QStringLiteral("Spline waypoint");
    case MissionWalk::NavLoiterTime: return QStringLiteral("Hold");
    case MissionWalk::NavLoiterTurns: return QStringLiteral("Circle");
    case MissionWalk::NavLoiterToAlt: return QStringLiteral("Loiter to altitude");
    case MissionWalk::NavDelay: return QStringLiteral("Delay");
    case MissionWalk::NavReturnToLaunch: return QStringLiteral("Return and land");
    case MissionWalk::NavLand: return QStringLiteral("Land");
    default: return QStringLiteral("Command %1").arg(command);
    }
}

QVariantMap VehicleProfileManager::missionEstimate()
{
    QVariantMap out;
    out[QStringLiteral("applicable")] = m_kind == VehicleKind::Multirotor;
    out[QStringLiteral("ready")] = false;
    out[QStringLiteral("result")] = int(MissionAssess::CantAssess);
    out[QStringLiteral("resultName")] = MissionAssess::resultName(MissionAssess::CantAssess);
    out[QStringLiteral("colour")] = MissionAssess::resultColour(MissionAssess::CantAssess);
    auto notReady = [&out](const QString &why) {
        out[QStringLiteral("problem")] = why;
        out[QStringLiteral("reasons")] = QStringList{why};
        return out;
    };
    const double emptyKg = emptyWeightKg();
    out[QStringLiteral("emptyKg")] = emptyKg;
    const PayloadRelease::Config drop = dropConfig();
    const double payloadKg = (m_payloadEntered && m_payloadWeightKg > 0.0) ? m_payloadWeightKg : 0.0;
    out[QStringLiteral("payloadKg")] = payloadKg;
    out[QStringLiteral("payloadCount")] = drop.payloads;
    if (m_kind != VehicleKind::Multirotor)
        return notReady(QStringLiteral("Copters only"));
    const MotorTable::Table *table = MotorTable::byId(m_motorTableId);
    if (!table || !m_telemetry)
        return notReady(QStringLiteral("No motor table linked to this copter"));
    Vehicle *v = m_telemetry->vehicle();
    MissionManager *mm = v ? v->missionManager() : nullptr;
    if (!mm || mm->missionItems().size() < 2)
        return notReady(QStringLiteral("No mission on the copter"));
    const double homeLat = m_telemetry->homeLatitude();
    const double homeLon = m_telemetry->homeLongitude();
    if (qAbs(homeLat) < 1e-6 && qAbs(homeLon) < 1e-6)
        return notReady(QStringLiteral("Home position not known yet"));
    const double homeAmsl = m_telemetry->homeAltitude();

    // The mission as the copter has it (item 0 is home).
    QVector<MissionWalk::Item> items;
    for (const MissionItem *mi : mm->missionItems()) {
        MissionWalk::Item it;
        it.seq = mi->sequenceNumber();
        it.frame = int(mi->frame());
        it.command = int(mi->command());
        it.p1 = mi->param1();
        it.p2 = mi->param2();
        it.p3 = mi->param3();
        it.p4 = mi->param4();
        it.lat = mi->param5();
        it.lon = mi->param6();
        it.alt = mi->param7();
        it.autocontinue = mi->autoContinue();
        items.append(it);
    }
    const MissionWalk::Settings ws = MissionWalk::Settings::read(copterParams(m_telemetry));
    const MissionWalk::Result w = MissionWalk::walk(items, ws, homeLat, homeLon, MissionWalk::Start(), homeAmsl);

    // The motor table, the pack and the air, as the battery estimate has them.
    const QVariantMap setup = motorTableSetup();
    const int motors = setup.value(QStringLiteral("motors")).toInt() > 0 ? setup.value(QStringLiteral("motors")).toInt()
                                                                         : table->motors;
    const int cells = setup.value(QStringLiteral("cells")).toInt() > 0 ? setup.value(QStringLiteral("cells")).toInt()
                                                                       : table->cells;
    MissionCost::Inputs in;
    in.table = table;
    in.motors = motors;
    in.pack = motorTablePack(cells);
    in.sigma = setup.value(QStringLiteral("densityRatio"), MotorTable::kTableDensityRatio).toDouble();
    in.calibration = setup.value(QStringLiteral("calibrationFactor"), 1.0).toDouble();
    in.emptyKg = emptyKg > 0.0 ? emptyKg : qQNaN();
    in.payloadKg = payloadKg;
    const double socNow = packRestingSocPct(m_telemetry->batteryVoltage());
    in.startSocPct = socNow > 0.0 ? socNow : 100.0;
    in.reserveSocPct = m_batteryReserve * 100.0;
    const QString uid = m_currentDeviceUid;
    in.measuredRatio = [uid](double air) { return storedCruiseRatio(uid, air); };
    in.currentLimitA = table->packCRating * capacityAh();
    auto param = [this](const char *name) {
        return m_telemetry->hasParameter(QLatin1String(name)) ? double(m_telemetry->parameterValue(QLatin1String(name)))
                                                              : copterParam(QLatin1String(name));
    };
    in.battLowV = param("BATT_LOW_VOLT");
    in.battCrtV = param("BATT_CRT_VOLT");

    // Where the payload comes off: the mission's own release commands on the
    // release output (or a gripper release), in order, one payload each;
    // otherwise the item the operator picked for an RC drop (all of it there);
    // otherwise it is carried home (the safe case).
    QString dropText;
    if (payloadKg > 0.0 && w.ok) {
        int found = 0;
        for (const MissionWalk::Event &e : w.events) {
            const bool servo = (e.command == MissionWalk::DoSetServo || e.command == MissionWalk::DoRepeatServo)
                               && drop.output > 0 && qRound(e.p1) == drop.output
                               && (drop.onWhenLow ? e.p2 < drop.thresholdUs : e.p2 > drop.thresholdUs);
            const bool gripper = e.command == MissionWalk::DoGripper && qRound(e.p2) == 0;
            if ((servo || gripper) && found < drop.payloads) {
                in.drops.append({e.timeS, drop.eachKg()});
                ++found;
            }
        }
        if (found > 0) {
            dropText = QStringLiteral("%1 drop command%2 in the mission").arg(found).arg(found > 1 ? "s" : "");
        } else {
            const int item = PreflightSettingsManager::instance() ? PreflightSettingsManager::instance()->payloadDropItem() : 0;
            for (const MissionWalk::Arrival &a : w.arrivals)
                if (item > 0 && a.seq == item && in.drops.isEmpty())
                    in.drops.append({a.timeS, payloadKg});
            dropText = in.drops.isEmpty()
                           ? (item > 0 ? QStringLiteral("Drop item %1 is not a waypoint of this mission: payload costed as carried home").arg(item)
                                       : QStringLiteral("No drop point given: payload costed as carried home"))
                           : QStringLiteral("Dropped by the pilot at item %1").arg(item);
        }
    } else if (payloadKg <= 0.0) {
        dropText = m_payloadEntered ? QStringLiteral("No payload") : QStringLiteral("Payload not entered");
    }
    out[QStringLiteral("dropText")] = dropText;

    // The weather at the highest point of the mission.
    double topM = 0.0;
    for (const MissionWalk::Arrival &a : w.arrivals)
        topM = std::max(topM, a.altM);
    MissionAssess::Context ctx;
    ctx.flightHeightM = topM;
    ctx.hasDrops = !in.drops.isEmpty();
    ctx.cruiseMeasured = false;
    ctx.tableProven = setup.value(QStringLiteral("proven")).toBool();
    QString weatherText = QStringLiteral("No weather for the site");
    if (WeatherProvider *wp = WeatherProvider::instance()) {
        const SiteWeather::Conditions wc =
            wp->conditionsAt(homeLat, homeLon, homeAmsl, topM, QDateTime::currentSecsSinceEpoch());
        if (wc.windKnown) {
            in.wind.speedMs = wc.windMs;
            in.wind.fromDeg = wc.windFromDeg;
            in.wind.gustMs = wc.gustMs;
            ctx.weatherKnown = true;
            ctx.gustMs = std::isfinite(wc.gustMs) ? wc.gustMs : wc.windMs;
            weatherText = QStringLiteral("Wind %1 m/s%2 at %3 m, from the %4%5")
                              .arg(wc.windMs, 0, 'f', 1)
                              .arg(std::isfinite(wc.gustMs) ? QStringLiteral(" gusting %1").arg(wc.gustMs, 0, 'f', 1) : QString())
                              .arg(qRound(topM)).arg(wc.windSource)
                              .arg(std::isfinite(wc.forecastAgeS) ? QStringLiteral(" (%1 old)").arg(SiteWeather::hoursText(wc.forecastAgeS)) : QString());
        }
        ctx.weatherConfidence = wc.confidence;
        ctx.weatherReasons = wc.reasons;
    }
    out[QStringLiteral("weatherText")] = weatherText;

    const MissionCost::Result cost = MissionCost::cost(w, ws, homeLat, homeLon, in);
    ctx.cruiseMeasured = cost.ok && cost.ratioMeasured;
    MissionCost::Result returned;
    if (ctx.hasDrops)
        returned = MissionCost::costPayloadReturned(w, ws, homeLat, homeLon, in);
    MissionAssess::Limits lim;
    lim.reservePct = in.reserveSocPct;
    lim.battLowV = in.battLowV;
    lim.battCrtV = in.battCrtV;
    lim.battLowAction = int(param("BATT_FS_LOW_ACT"));
    lim.battCrtAction = int(param("BATT_FS_CRT_ACT"));
    if (PreflightSettingsManager::instance())
        lim.gustLimitMs = PreflightSettingsManager::instance()->windThresholdGust();
    const MissionAssess::Assessment a = MissionAssess::assess(w, cost, ctx.hasDrops ? &returned : nullptr, lim, ctx);

    out[QStringLiteral("ready")] = a.result != MissionAssess::CantAssess;
    out[QStringLiteral("problem")] = a.problem;
    out[QStringLiteral("result")] = a.result;
    out[QStringLiteral("resultName")] = MissionAssess::resultName(a.result);
    out[QStringLiteral("colour")] = MissionAssess::resultColour(a.result);
    out[QStringLiteral("landLikelyPct")] = a.landLikelyPct;
    out[QStringLiteral("landPessimisticPct")] = a.landPessimisticPct;
    out[QStringLiteral("totalS")] = a.totalS;
    out[QStringLiteral("totalText")] = clockText(a.totalS);
    out[QStringLiteral("confidence")] = SiteWeather::confidenceName(a.confidence);
    out[QStringLiteral("confidenceWhy")] = a.confidenceWhy;
    out[QStringLiteral("reasons")] = a.reasons;
    out[QStringLiteral("weatherRed")] = a.weatherRed;
    out[QStringLiteral("overCurrent")] = a.overCurrent;
    QStringList assumed;
    if (!ctx.cruiseMeasured)
        assumed << QStringLiteral("cruise power (hover in the likely case, %1× in the pessimistic case)")
                       .arg(MissionCost::kUnmeasuredPessimisticRatio, 0, 'f', 1);
    assumed << QStringLiteral("climb: half of the extra power lost");
    if (w.returnAssumed)
        assumed << QStringLiteral("RTL from the mission's last point");
    if (in.drops.isEmpty() && payloadKg > 0.0)
        assumed << QStringLiteral("payload carried home");
    out[QStringLiteral("assumed")] = assumed;

    QVariantList wps;
    for (const MissionAssess::Waypoint &p : a.waypoints) {
        int command = MissionWalk::NavReturnToLaunch;
        for (const MissionWalk::Arrival &ar : w.arrivals)
            if (ar.seq == p.seq && qAbs(ar.timeS - p.timeS) < 1e-6)
                command = ar.command;
        QVariantMap m;
        m[QStringLiteral("seq")] = p.seq;
        m[QStringLiteral("what")] = commandText(command, p.seq);
        m[QStringLiteral("timeText")] = clockText(p.timeS);
        m[QStringLiteral("socLikelyPct")] = p.socLikelyPct;
        m[QStringLiteral("socPessimisticPct")] = p.socPessimisticPct;
        m[QStringLiteral("state")] = p.state;
        m[QStringLiteral("stateName")] = MissionAssess::stateName(p.state);
        wps.append(m);
    }
    out[QStringLiteral("waypoints")] = wps;
    return out;
}
