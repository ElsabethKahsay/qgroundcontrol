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
#include "MissionRecord.h"
#include "MissionManager/MissionManager.h"
#include "MissionManager/MissionItem.h"
#include "Terrain/TerrainQuery.h"
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

// The MAVLink command of the mission item the autopilot is flying now;
// -1 when it is not known. Shared by the trip check and the mission estimate
// so "returning" is decided the same way in both.
static int currentMissionCommand(MissionManager *mm)
{
    if (!mm)
        return -1;
    for (const MissionItem *mi : mm->missionItems())
        if (mi->sequenceNumber() == mm->currentIndex())
            return int(mi->command());
    return -1;
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
        // The vehicle's key is known only once the registry has settled its
        // identity, possibly after the link counts as connected, and changes
        // when a hardware id arrives later. Follow it; a re-key that moved the
        // rows already moved this manager (onVehicleReKeyed), so nothing reloads twice.
        disconnect(VehicleRegistry::instance(), &VehicleRegistry::identitySettled, this, nullptr);
        connect(VehicleRegistry::instance(), &VehicleRegistry::identitySettled, this, [this]() {
            if (!m_telemetry || !m_telemetry->isConnected())
                return;
            const QString key = resolveDeviceUid();
            if (!key.isEmpty() && key != m_currentDeviceUid)
                onConnectionChanged();
        });
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
        if (m_liveSOC != -1 || m_liveTimeMins != -1.0 || !m_powerLive.isEmpty() || !m_missionLive.isEmpty()) {
            m_liveSOC = -1;
            m_liveTimeMins = -1.0;
            m_liveSmoothAmps = 0.0;
            m_powerLive.clear();
            m_missionLive.clear();
            emit liveDataChanged();
        }
        m_liveWasArmed = false;
        m_livePayloadAboard = false;
        m_liveSeenDisarmed = false;
        m_terrainLegs.clear();   // another copter's mission; the profiles (by place) stay
        m_fcCurrent = FcCurrentCheck();
        m_fcCheckLastMs = 0;
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
        if (!m_motorTableId.isEmpty() || !m_motorTableWaivedBy.isEmpty()) {
            m_motorTableId.clear();
            m_motorTableWaivedBy.clear();   // stays stored for that copter
            m_packOhm = -1.0;
            emit motorTableChanged();
        }
        emit currentVehicleChanged();
        return;
    }

    // Connected: resolve device UID
    m_currentDeviceUid = resolveDeviceUid();
    if (m_currentDeviceUid.isEmpty()) {
        // The registry settles the vehicle's identity on AUTOPILOT_VERSION
        // (at most VehicleRegistry::kSettleTimeoutMs); identitySettled brings
        // this back with the key.
        qInfo() << "VehicleProfileManager: waiting for the vehicle's identity";
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

    m_fcCurrent = FcCurrentCheck();
    m_fcCheckLastMs = 0;
    _loadVehicleProfile(apType, afType);

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
void VehicleProfileManager::_loadVehicleProfile(const QString &apType, const QString &afType)
{
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

    m_motorTableId = DatabaseManager::instance().vehicleMotorTableId(m_currentDeviceUid);
    m_motorTableWaivedBy = QSettings().value(QStringLiteral("MotorTable/waivedBy/%1").arg(m_currentDeviceUid)).toString();
    m_fcCurrent.dead = m_fcCurrent.dead
        || QSettings().value(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(m_currentDeviceUid), false).toBool();
    m_packOhm = QSettings().value(QStringLiteral("MotorTable/packOhm/%1").arg(m_currentDeviceUid), -1.0).toDouble();
    emit motorTableChanged();
}

void VehicleProfileManager::onVehicleReKeyed(const QString &oldKey, const QString &newKey)
{
    // Emitted by the registry after the database has moved the copter's rows
    // and settings to newKey; reloading before that would find nothing.
    if (oldKey.isEmpty() || newKey.isEmpty() || oldKey != m_currentDeviceUid)
        return;
    m_currentDeviceUid = newKey;
    _loadVehicleProfile(autopilotTypeString(), airframeTypeString());
    emit currentVehicleChanged();
}

QString VehicleProfileManager::resolveDeviceUid()
{
    if (!m_telemetry) return {};
    Vehicle *v = m_telemetry->vehicle();
    if (!v) return {};

    // The registry's key for the connected vehicle (decimal uid, uid2 hex or
    // fallback fingerprint), the same key its vehicles row uses. Empty until
    // the registry has settled the identity (AUTOPILOT_VERSION or its
    // timeout); the profile then loads on identitySettled.
    Q_UNUSED(v)
    return VehicleRegistry::instance()->currentDeviceKey();
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

// The pack's charge from the count at the arm less the mAh used. The power
// estimate's socNowPct and the mission estimate's starting charge share this
// arithmetic: passing this tick's value to _updateMissionLive (instead of the
// previous update's m_powerLive.socNowPct) keeps them from disagreeing by 2 s.
double VehicleProfileManager::socNowPctFromCount(double armStartSocPct, double mahUsedMah, double factor, double capAh)
{
    if (mahUsedMah < 0.0 || capAh <= 0.0)
        return -1.0;
    return (capAh * std::clamp(armStartSocPct, 0.0, 100.0) / 100.0 - mahUsedMah / 1000.0 * factor) / capAh * 100.0;
}

// The mAh used and factor counted on this update: the GCS's own count when
// there is no measuring current sensor, else the controller's counter since
// the arm. The same inputs feed _updatePowerLive's socNowPct and the mission
// estimate's charge.
VehicleProfileManager::ChargeCount VehicleProfileManager::chargeCount(double fcMahUsed) const
{
    ChargeCount c;
    const bool armed = m_telemetry && m_telemetry->armed();
    const int monitor = m_telemetry && m_telemetry->hasParameter(QStringLiteral("BATT_MONITOR"))
        ? qRound(m_telemetry->parameterValue(QStringLiteral("BATT_MONITOR"), -1.0f)) : -1;
    c.gcsCount = monitor == -1 || monitor == 0 || monitor == 3 || monitor == 25 || m_fcCurrent.dead;
    c.factor = c.gcsCount ? motorTableSetup().value(QStringLiteral("calibrationFactor"), 1.0).toDouble() : 1.0;
    if (!armed)
        c.mahUsed = 0.0;
    else if (c.gcsCount)
        c.mahUsed = m_gcsCountKnown ? m_gcsUsedAh * 1000.0 : -1.0;
    else if (m_gcsCountKnown && fcMahUsed >= 0.0 && m_fcMahAtArm >= 0.0)
        c.mahUsed = std::max(0.0, fcMahUsed - m_fcMahAtArm);
    return c;
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
    // The in-flight mission estimate starts from the battery estimate's
    // charge (its own count from the arm, the same figure the Fly view shows)
    // when it has one; the flight controller's otherwise, which is no use
    // when its current is not measured. After a mid-flight join with a dead
    // sensor the FC's % is 100 all flight: never let the mission cost a charge
    // the GCS cannot know (its RETURN NOW would disappear).
    if (chargeUnknown(m_telemetry && armed, m_fcCurrent.dead, m_gcsCountKnown)) {
        _updateMissionLive(-1, true);
    } else {
        // This update's charge comes from the same function _updatePowerLive
        // uses for socNowPct. For the controller's counter that is this tick's
        // reading; the GCS's own count only advances inside _updatePowerLive,
        // which runs after the mission update, so that count is one update
        // behind (about 0.06 % of the pack). The chargeUnknown guard above is
        // untouched.
        const ChargeCount count = chargeCount(mahUsed);
        const double estSoc = m_powerLive.value(QStringLiteral("active")).toBool()
            ? socNowPctFromCount(m_armStartSocPct, count.mahUsed, count.factor, capacityAh())
            : -1.0;
        _updateMissionLive(estSoc >= 0.0 ? qRound(estSoc) : soc);
    }
    _updatePowerLive(mahUsed, m_telemetry->batterySocPct());
}

QString VehicleProfileManager::chargeUnknownWhy()
{
    return QStringLiteral("charge unknown: the GCS joined during the flight and the current sensor reads 0 A");
}

QVariantMap VehicleProfileManager::missionLiveChargeUnknown()
{
    QVariantMap out;
    out[QStringLiteral("live")] = true;
    out[QStringLiteral("ready")] = false;
    out[QStringLiteral("chargeUnknown")] = true;
    out[QStringLiteral("result")] = int(MissionAssess::CantAssess);
    out[QStringLiteral("resultName")] = MissionAssess::resultName(MissionAssess::CantAssess);
    out[QStringLiteral("colour")] = MissionAssess::resultColour(MissionAssess::CantAssess);
    out[QStringLiteral("problem")] = QStringLiteral("charge unknown (joined in flight)");
    out[QStringLiteral("reasons")] = QStringList{out.value(QStringLiteral("problem")).toString()};
    out[QStringLiteral("returnNow")] = false;
    return out;
}

bool VehicleProfileManager::displaySocFromCount() const
{
    const bool armed = m_telemetry && m_telemetry->armed();
    return socFromGcsCount(powerLiveActive(), armed, m_fcCurrent.dead, m_gcsCountKnown,
                           m_powerLive.value(QStringLiteral("socNowPct"), -1.0).toDouble())
        || restingSocShown(powerLiveActive(), armed, m_fcCurrent.dead, displayRestingSocPct());
}

int VehicleProfileManager::displaySocPct() const
{
    return displaySocPctFor(powerLiveActive(), m_telemetry && m_telemetry->armed(),
                            m_fcCurrent.dead, m_gcsCountKnown,
                            m_powerLive.value(QStringLiteral("socNowPct"), -1.0).toDouble(),
                            m_liveSOC, displayRestingSocPct());
}

double VehicleProfileManager::displayRestingSocPct() const
{
    // packRestingSocPct() answers 100 when it has nothing to go on; here
    // "nothing to go on" must stay unknown, so check for a reading first.
    const double volts = m_telemetry ? m_telemetry->batteryVoltage() : 0.0;
    return (volts > 1.0 && m_batterySeriesCells > 0) ? packRestingSocPct(volts) : -1.0;
}

QPair<double, double> VehicleProfileManager::recordedGcsCharge() const
{
    if (!m_fcCurrent.dead || !m_gcsCountKnown)
        return {-1.0, -1.0};
    const double mah = m_gcsUsedAh * 1000.0;   // raw count, as stored in motor_table_flights
    const double capAh = capacityAh();
    const double pct = capAh > 0.0
        ? (capAh * std::clamp(m_armStartSocPct, 0.0, 100.0) / 100.0 - m_gcsUsedAh) / capAh * 100.0
        : -1.0;
    return {mah, pct};
}

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
        const MissionRecord::Record rec = MissionRecord::fromJson(m.value(QStringLiteral("missionJson")).toString());
        m[QStringLiteral("missionSummary")] = MissionRecord::summary(rec);
        out.prepend(m);
    }
    return out;
}

bool VehicleProfileManager::setMotorTableFlightCharger(int flightId, double chargerMah)
{
    const double capMah = capacityAh() * 1000.0;
    // One mistyped value must not be able to shove the calibration to its
    // clamp: at most 120 % of the pack when its capacity is known.
    if (!chargerMahPlausible(chargerMah, capMah)) {
        m_chargerEntryError = QStringLiteral("More than 120% of the %1 mAh pack (max %2 mAh).")
                                  .arg(capMah, 0, 'f', 0).arg(1.2 * capMah, 0, 'f', 0);
        return false;
    }
    if (!DatabaseManager::instance().setMotorTableFlightCharger(flightId, chargerMah)) {
        m_chargerEntryError = QStringLiteral("Charger number not saved: 1–200000 mAh.");
        return false;
    }
    m_chargerEntryError.clear();
    emit motorTableChanged();
    return true;
}

bool VehicleProfileManager::dismissMotorTableFlight(int flightId)
{
    if (!DatabaseManager::instance().setMotorTableFlightState(flightId, QStringLiteral("dismissed")))
        return false;
    emit motorTableChanged();
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
    int cells = MotorTable::cellsFromVoltMax(watchedParam(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0));
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
        const double learn = watchedParam(QStringLiteral("MOT_HOVER_LEARN"), 0.0);
        const double h = watchedParam(QStringLiteral("MOT_THST_HOVER"), -1.0);
        if (learn >= 1.0 && h > 0.0 && std::abs(h - MotorTable::kDefaultThstHover) > 1e-4) {
            MotorTable::HoverParams p;
            p.thstHover = h;
            p.spinMin = watchedParam(QStringLiteral("MOT_SPIN_MIN"), p.spinMin);
            p.spinMax = watchedParam(QStringLiteral("MOT_SPIN_MAX"), p.spinMax);
            p.expo = watchedParam(QStringLiteral("MOT_THST_EXPO"), p.expo);
            p.battVoltMax = watchedParam(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0);
            p.battVoltMin = watchedParam(QStringLiteral("MOT_BAT_VOLT_MIN"), 0.0);
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
    // current (1 until three flights have a charger number).
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
    if (m_fcCurrent.dead) {
        issues.append(QVariantMap{{QStringLiteral("level"), QStringLiteral("warn")},
                                  {QStringLiteral("text"), QStringLiteral("The flight controller's current reads about 0 A "
                                                                          "in flight: the GCS counts the charge itself")}});
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
    // Which outputs drive motors (SERVOn_FUNCTION 33–40), and how many motors
    // the frame has (FRAME_CLASS).
    QVector<int> functions;
    for (int n = 1; n <= 16; ++n)
        functions.append(qRound(watchedParam(QStringLiteral("SERVO%1_FUNCTION").arg(n), 0.0)));
    const QVector<int> channels = MotorTable::motorChannels(functions);
    const int frameClass = qRound(watchedParam(QStringLiteral("FRAME_CLASS"), -1.0));
    const int expected = frameClass > 0 ? std::max(0, HardwareTestController::motorCountFromFrameClass(frameClass)) : 0;
    const MotorTable::EscRange range = MotorTable::escRange(watchedParam(QStringLiteral("MOT_PWM_MIN"), 0.0),
                                                            watchedParam(QStringLiteral("MOT_PWM_MAX"), 0.0));
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

// The battery estimate for the Fly view (PowerTypeModel::liveEstimate): the
// linked motor table (MotorTable.h) turning each motor's throttle into
// battery current, the selected pack, the charge used (the GCS's count, or a
// current sensor's), the copter's position, home, and the target saved on
// the gimbal page. The payload counts as aboard from the arm until the
// copter first comes within 20 m of the target.
void VehicleProfileManager::_updatePowerLive(double fcMahUsed, int socPct)
{
    QVariantMap out;
    const bool armed = m_telemetry && m_telemetry->armed();
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    const MotorTable::Table *table = m_kind == VehicleKind::Multirotor ? MotorTable::byId(m_motorTableId) : nullptr;
    const bool applicable = v && m_kind == VehicleKind::Multirotor && !batteryPackWaived() && table;

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
        // A new flight: the take-off prediction to compare against, nothing
        // reached or alerted yet.
        m_flightStartMs = nowMs;
        m_passed.clear();
        m_lastMissionIndex = -1;
        m_lastAlertResult = -1;
        m_alertedOldWeather = false;
        m_alertedGpsPoor = false;
        m_windSamples.clear();
        m_windEstN = m_windEstE = std::numeric_limits<double>::quiet_NaN();
        m_windKLearnedThisFlight = false;
        m_windLastMs = 0;
        m_windK = QSettings().value(QStringLiteral("WindFromLean/k/%1").arg(m_currentDeviceUid), 0.0).toDouble();
        m_dropEvents.clear();
        m_takeoffEstimate = missionEstimate();
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
            // The mission estimate against this flight (when one was made at
            // take-off): predicted and real by item, the landing, the drops.
            if (m_takeoffEstimate.value(QStringLiteral("ready")).toBool() && m_flightStartMs > 0) {
                MissionRecord::Record rec;
                rec.resultAtTakeoff = m_takeoffEstimate.value(QStringLiteral("resultName")).toString();
                rec.predTotalS = m_takeoffEstimate.value(QStringLiteral("totalS")).toDouble();
                rec.predLandLikely = m_takeoffEstimate.value(QStringLiteral("landLikelyPct")).toDouble();
                rec.predLandPess = m_takeoffEstimate.value(QStringLiteral("landPessimisticPct")).toDouble();
                rec.realTotalS = (nowMs - m_flightStartMs) / 1000.0;
                // The battery estimate's charge (its own count from the
                // arm), else the flight controller's.
                const double estSoc = m_powerLive.value(QStringLiteral("socNowPct"), -1.0).toDouble();
                rec.realLandSoc = estSoc >= 0.0 ? estSoc : m_liveSOC >= 0 ? double(m_liveSOC) : -1.0;
                for (const QVariant &wv : m_takeoffEstimate.value(QStringLiteral("waypoints")).toList()) {
                    const QVariantMap wp = wv.toMap();
                    MissionRecord::Item it;
                    it.seq = wp.value(QStringLiteral("seq")).toInt();
                    it.predTimeS = wp.value(QStringLiteral("timeS")).toDouble();
                    it.predSocLikely = wp.value(QStringLiteral("socLikelyPct")).toDouble();
                    it.predSocPess = wp.value(QStringLiteral("socPessimisticPct")).toDouble();
                    for (const QVariant &pv : m_passed) {
                        const QVariantMap p = pv.toMap();
                        if (p.value(QStringLiteral("seq")).toInt() == it.seq && it.seq >= 0) {
                            it.realTimeS = p.value(QStringLiteral("timeS")).toDouble();
                            it.realSoc = p.value(QStringLiteral("socPct")).toDouble();
                        }
                    }
                    rec.items.append(it);
                }
                rec.drops = m_dropEvents;
                f[QStringLiteral("missionJson")] = MissionRecord::toJson(rec);
            }
            DatabaseManager::instance().insertMotorTableFlight(f);
        }
        m_tableLog = TableFlightLog();
        if (table && m_hoverSamples >= MotorTable::kMinHoverSamples)
            DatabaseManager::instance().saveVehicleMotorHover(m_currentDeviceUid, m_hoverThrustSum / m_hoverSamples,
                                                              m_hoverSamples);
        m_hoverThrustSum = 0.0;
        m_hoverSamples = 0;
        m_missionReturnNowRaw = false;
        m_missionReturnHeld = false;
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
        // A drag constant learned on this flight (it turned enough) is kept
        // for the next ones: then the wind needs no turns.
        if (m_windKLearnedThisFlight && m_windK > 0.0)
            QSettings().setValue(QStringLiteral("WindFromLean/k/%1").arg(m_currentDeviceUid), m_windK);
        m_windKLearnedThisFlight = false;
        if (table && m_packOhmSamples >= MotorTable::kMinHoverSamples && m_packOhmLive > 0.0) {
            m_packOhm = m_packOhmLive;
            QSettings().setValue(QStringLiteral("MotorTable/packOhm/%1").arg(m_currentDeviceUid), m_packOhm);
        }
    }
    m_liveWasArmed = armed;
    // Set once the arm transition above has decided whether the count covers
    // this flight (a GCS that joined armed has no count). The dead-sensor flag
    // is refreshed further down, in step with the flight's current readings.
    bool unknownCharge = false;
    if (m_livePayloadAboard && !target.isEmpty() && pos.isValid()
        && PowerTypeModel::distanceM(pos.latitude(), pos.longitude(), target.value(QStringLiteral("lat")).toDouble(),
                                     target.value(QStringLiteral("lon")).toDouble()) <= PowerTypeModel::kDropRadiusM)
        m_livePayloadAboard = false;

    // The table's setup checks: a different frame or pack, or no pack size,
    // turns the estimate off.
    const QVariantMap setup = table ? motorTableSetup() : QVariantMap();
    const bool tableReady = applicable && !setup.value(QStringLiteral("off")).toBool();
    if (tableReady) {
        PowerTypeModel::LiveInput in;
        in.capacityAh = capacityAh();
        in.reserve = m_batteryReserve;
        in.socPct = socPct;
        in.startSocPct = m_armStartSocPct;
        in.groundSpeedMs = m_telemetry->groundSpeed();

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
                p.thstHover = std::max(0.001, overall.toDouble() / 100.0);
                p.spinMin = watchedParam(QStringLiteral("MOT_SPIN_MIN"), p.spinMin);
                p.spinMax = watchedParam(QStringLiteral("MOT_SPIN_MAX"), p.spinMax);
                p.expo = watchedParam(QStringLiteral("MOT_THST_EXPO"), p.expo);
                p.battVoltMax = watchedParam(QStringLiteral("MOT_BAT_VOLT_MAX"), 0.0);
                p.battVoltMin = watchedParam(QStringLiteral("MOT_BAT_VOLT_MIN"), 0.0);
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
        // Whether the flight controller's current is measured, judged in
        // flight against the table (fcCurrentCheck): a monitor type with a
        // current sensor can still report 0 A.
        {
            const double altNow = m_telemetry->altitudeRelative();
            const bool airborneNow = haveNow && std::isfinite(altNow) && altNow > PowerTypeModel::kAirborneAltM;
            const double dtS = m_fcCheckLastMs > 0 ? (nowMs - m_fcCheckLastMs) / 1000.0 : 0.0;
            m_fcCheckLastMs = nowMs;
            const FcCurrentCheck next = fcCurrentCheck(m_fcCurrent, airborneNow, haveNow ? now.amps : 0.0,
                                                       m_telemetry->batteryCurrentAmps(), dtS);
            const bool changed = next.dead != m_fcCurrent.dead;
            m_fcCurrent = next;
            if (changed)
                setFcCurrentDead(next.dead);
        }
        // A current sensor (BATT_MONITOR other than 0, 3 or 25) that is
        // measuring wins; the table is the count otherwise. The same count
        // feeds the mission estimate's charge on this tick.
        const ChargeCount count = chargeCount(fcMahUsed);
        const bool gcsCount = count.gcsCount;
        // Dead sensor and a count that does not cover the flight: the FC's %
        // (100 with a dead monitor) is not a charge reading. Keep the charge
        // fields unknown instead of falling back to it.
        unknownCharge = chargeUnknown(armed, m_fcCurrent.dead, m_gcsCountKnown);
        if (unknownCharge)
            in.socPct = -1.0;
        // Motor-output readings are counted as they arrive (_onMotorOutputs);
        // only from the overall throttle does this update count the charge.
        // Counted whatever the source in use, so that the GCS's own count is
        // whole from the arm when a sensor is found not to measure in flight.
        m_lastSigma = sigma;
        const bool readingsCounted = fromOutputs && m_readings > 0;
        if (m_gcsCounting && m_gcsLastMs > 0 && haveNow && !readingsCounted)
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
        // this copter needs to hover in this air (its measured hover thrust:
        // cruiseReferenceThrustG), kept in the band of the airspeed.
        const double hoverThrust = cruiseReferenceThrustG(m_hoverThrustSum, m_hoverSamples,
                                                          setup.value(QStringLiteral("hoverThrustG")).toDouble(),
                                                          setup.value(QStringLiteral("hoverSource")).toString());
        // Not while the GPS is unreliable: the speed that picks the band and
        // tells cruise from hover comes from it.
        if (haveNow && fromOutputs && !m_livePayloadAboard && packV > 0.0 && hoverThrust > 0.0
            && std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM && !m_telemetry->gpsPoor()
            && MotorTable::isCruiseSample(gsNow, climb)) {
            const int n = static_cast<int>(thr.size());
            const double hoverW = n * MotorTable::powerForThrustAt(*table, hoverThrust, sigma).watts;
            if (hoverW > 0.0) {
                const double ratio = (now.amps - MotorTable::kElectronicsA) * packV / hoverW;
                m_cruiseRatioSum += ratio;
                ++m_cruiseSamples;
                // Kept by airspeed (the costing looks the ratio up by the
                // leg's airspeed): the ground velocity against the wind
                // from the lean when it is known, the ground speed
                // otherwise (the same in still air).
                const double air = WindFromLean::airspeedMs(m_telemetry->velocityNorthMs(), m_telemetry->velocityEastMs(),
                                                            m_windEstN, m_windEstE, gsNow);
                QPair<double, int> &band = m_cruiseBands[MotorTable::cruiseBand(air)];
                band.first += ratio;
                ++band.second;
                m_tableLog.cruiseS += 2.0;
            }
        }
        // The fine-tuning from the charger comparison: the table's count
        // and current × the factor (a current sensor's count is not).
        const double calFactor = setup.value(QStringLiteral("calibrationFactor"), 1.0).toDouble();
        in.factor = count.factor;
        in.currentA = -1.0;
        in.throttleRatio = 1.0;
        in.fcMahUsed = count.mahUsed;

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
        const double socNow = socNowPctFromCount(m_armStartSocPct, in.fcMahUsed, in.factor, capAh);
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
        out[QStringLiteral("chargeSource")] = unknownCharge ? QStringLiteral("unknown")
                                            : !gcsCount ? QStringLiteral("fc")
                                            : in.fcMahUsed >= 0.0 ? QStringLiteral("gcs") : QStringLiteral("fcPercent");
        out[QStringLiteral("chargeUnknown")] = unknownCharge;
        out[QStringLiteral("chargeUnknownWhy")] = unknownCharge ? chargeUnknownWhy() : QString();
        out[QStringLiteral("tableCurrentA")] = haveNow ? now.amps : -1.0;
        out[QStringLiteral("motorsBeyond")] = haveNow ? now.motorsBeyond : 0;
        out[QStringLiteral("hoverSource")] = setup.value(QStringLiteral("hoverSource"));
        out[QStringLiteral("densityRatio")] = sigma;
        out[QStringLiteral("densitySource")] = setup.value(QStringLiteral("densitySource"));
        out[QStringLiteral("throttleSource")] = fromOutputs ? QStringLiteral("motor outputs")
                                              : thr.isEmpty() ? QStringLiteral("none") : QStringLiteral("overall throttle");
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
        in.cruiseSpeedMs = setup.value(QStringLiteral("cruiseSpeedMs"), -1.0).toDouble();
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
        // With a mission estimated in flight, RETURN NOW comes from it (the
        // turn-back point); the out-and-back to a target only without one.
        const bool fromMission = m_missionLive.value(QStringLiteral("ready")).toBool();
        // The trip to a target: no RETURN NOW once already returning or
        // landing (there is nothing to return from); the landing using the
        // reserve is said instead.
        const int currentCommand = currentMissionCommand(v ? v->missionManager() : nullptr);
        const bool returning = isReturning(m_telemetry->flightMode(), currentCommand);
        TripAdvice advice = tripAdvice(r.returnNow, returning);
        // Held with hysteresis (returnNowHeld): the margin is what is left
        // above the reserve after the rest of the trip, in points of the pack.
        const double tripMarginPct = (r.tripKnown && in.capacityAh > 0.0)
                                         ? (r.usableAh - r.tripNeedAh) / in.capacityAh * 100.0
                                         : qQNaN();
        const bool tripWasOn = m_powerLive.value(QStringLiteral("returnNowFrom")).toString() == QLatin1String("trip")
                               && m_powerLive.value(QStringLiteral("returnNow")).toBool();
        advice.returnNow = armed && r.airborne && returnNowHeld(tripWasOn, tripMarginPct, returning);
        // No charge-based RETURN NOW from a charge we cannot know. Distance
        // home is still known, but there is no trip advice to cost against it.
        out[QStringLiteral("returnNow")] = unknownCharge ? false
                                         : fromMission ? m_missionLive.value(QStringLiteral("returnNow")).toBool()
                                                       : advice.returnNow;
        out[QStringLiteral("landingOnReserve")] = !unknownCharge && !fromMission && advice.landingOnReserve;
        out[QStringLiteral("returnNowFrom")] = fromMission ? QStringLiteral("mission") : QStringLiteral("trip");
        out[QStringLiteral("returning")] = returning;
        out[QStringLiteral("returnNowWhy")] = fromMission ? m_missionLive.value(QStringLiteral("returnNowWhy")) : QVariant(QString());
        out[QStringLiteral("payloadAboard")] = in.payloadAboard;
        out[QStringLiteral("payloadAboardKg")] = in.payloadAboard ? in.payloadKg : 0.0;
        out[QStringLiteral("payloadsReleased")] = m_drop.released();
        const PayloadRelease::Config drop = dropConfig();
        out[QStringLiteral("payloadCount")] = drop.payloads;
        out[QStringLiteral("dropOutputSet")] = drop.isSet();
        // m_armStartSocPct is only measured while disarmed: after a join it is
        // still its default 100, which is not a start charge. Say unknown.
        out[QStringLiteral("startSocPct")] = (armed && !m_gcsCountKnown) ? -1.0 : in.startSocPct;
        out[QStringLiteral("source")] = QStringLiteral("motor table");
        out[QStringLiteral("typeName")] = table->name;
        out[QStringLiteral("tableId")] = table->id;
    } else {
        out[QStringLiteral("active")] = false;
    }
    // In the log for after-flight review: when the estimate comes on, and
    // each time the reason it is off changes (the reason is also kept in the
    // map, so the next update can tell whether it changed).
    const bool isActive = out.value(QStringLiteral("active")).toBool();
    if (!isActive)
        out[QStringLiteral("offReason")] =
            unknownCharge ? QStringLiteral("charge unknown (joined in flight)")
            : !v ? QStringLiteral("no vehicle")
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
            : !m_motorTableWaivedBy.isEmpty() ? QStringLiteral("motor table waived")
            : QStringLiteral("no motor table linked");
    const bool wasActive = m_powerLive.value(QStringLiteral("active")).toBool();
    const QString why = out.value(QStringLiteral("offReason")).toString();
    if (wasActive != isActive || m_powerLive.isEmpty()
        || (!isActive && why != m_powerLive.value(QStringLiteral("offReason")).toString())) {
        qInfo().noquote() << QStringLiteral("VehicleProfileManager: battery estimate %1")
                                 .arg(isActive ? QStringLiteral("on (%1, charge used from %2)")
                                                     .arg(out.value(QStringLiteral("typeName")).toString(),
                                                          out.value(QStringLiteral("chargeSource")).toString())
                                               : QStringLiteral("off: ") + why);
    }
    // An unreliable GPS: the distance left and anything from the position
    // may be wrong (the charge is not). Said once per flight, in the air.
    const bool gpsPoor = m_telemetry && m_telemetry->gpsPoor();
    out[QStringLiteral("gpsPoor")] = gpsPoor;
    if (gpsPoor && armed && !m_alertedGpsPoor && m_telemetry
        && m_telemetry->altitudeRelative() > PowerTypeModel::kAirborneAltM) {
        m_alertedGpsPoor = true;
        qInfo().noquote() << QStringLiteral("VehicleProfileManager: alert: GPS unreliable in flight");
        emit missionAlert(QStringLiteral("GPS unreliable: the position, the distances and the return may be wrong"), true);
    }
    // RETURN NOW from the mission is logged with its reason by
    // _updateMissionLive; here only that it came from there. From the trip,
    // its numbers.
    if (m_powerLive.value(QStringLiteral("returnNow")).toBool() != out.value(QStringLiteral("returnNow")).toBool()) {
        const QString onOff = out.value(QStringLiteral("returnNow")).toBool() ? QStringLiteral("on") : QStringLiteral("off");
        if (out.value(QStringLiteral("returnNowFrom")).toString() == QLatin1String("mission"))
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: RETURN NOW %1 (from the mission estimate)").arg(onOff);
        else if (out.value(QStringLiteral("returnNow")).toBool())
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: RETURN NOW on (trip needs %1 Ah, %2 Ah left)")
                                     .arg(out.value(QStringLiteral("tripNeedAh")).toDouble(), 0, 'f', 2)
                                     .arg(out.value(QStringLiteral("usableAh")).toDouble(), 0, 'f', 2);
        else
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: RETURN NOW off (%1)")
                                     .arg(returnNowOffWhy(out, out.value(QStringLiteral("returning")).toBool()));
    }
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



// ── Motor table link ─────────────────────────────────────────────────────

bool VehicleProfileManager::linkCurrentVehicleMotorTable(const QString &tableId)
{
    if (m_currentDeviceUid.isEmpty() || (!tableId.isEmpty() && !MotorTable::byId(tableId))
        || !DatabaseManager::instance().linkVehicleMotorTable(m_currentDeviceUid, tableId))
        return false;
    m_motorTableId = DatabaseManager::instance().vehicleMotorTableId(m_currentDeviceUid);
    m_tableSmoothA = 0.0;
    if (!m_motorTableId.isEmpty())
        clearMotorTableWaiver();   // the table replaces the waiver
    emit motorTableChanged();
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

// ── Per-copter motor-table waiver ─────────────────────────────────────────
// The "Motor table" checklist item reads it. It belongs to the copter (kept
// in QSettings by its device UID, loaded on each connection) and lasts until
// a motor table is linked to it or it is withdrawn.

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

bool VehicleProfileManager::waiveMotorTable(const QString &pilot)
{
    if (m_currentDeviceUid.isEmpty())
        return false;   // the waiver is kept per copter
    if (!recordWaiver(QStringLiteral("power_type_waived"), pilot,
                      QStringLiteral("No motor table for this copter until one is linked; "
                                     "rough flight-time estimate, no mission estimate"),
                      QStringLiteral("power.power_type")))
        return false;
    m_motorTableWaivedBy = pilot.trimmed();
    QSettings().setValue(QStringLiteral("MotorTable/waivedBy/%1").arg(m_currentDeviceUid), m_motorTableWaivedBy);
    emit motorTableChanged();
    return true;
}

void VehicleProfileManager::clearMotorTableWaiver()
{
    if (!m_currentDeviceUid.isEmpty())
        QSettings().remove(QStringLiteral("MotorTable/waivedBy/%1").arg(m_currentDeviceUid));
    if (m_motorTableWaivedBy.isEmpty())
        return;
    m_motorTableWaivedBy.clear();
    emit motorTableChanged();
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

QVariantMap VehicleProfileManager::missionCheck()
{
    QVariantMap out;
    out[QStringLiteral("applicable")] = m_kind == VehicleKind::Multirotor;
    if (m_kind != VehicleKind::Multirotor)
        return out;
    const MotorTable::Table *table = MotorTable::byId(m_motorTableId);
    out[QStringLiteral("linked")] = table != nullptr;
    out[QStringLiteral("packWaived")] = batteryPackWaived();
    if (!table)
        return out;

    PowerTypeModel::Model model;
    double cruise = 10.0;
    bool speedAssumed = true;
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
    // Proven on this copter by its charger numbers (the trip's figures
    // then count as validated; nothing here holds the arm).
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

// ── Fleet settings ─────────────────────────────────────────────────────────
// The QSettings keys keep their old "PowerTypes/" group so saved values carry
// over.

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
    emit motorTableChanged();
    return true;
}

bool VehicleProfileManager::setLandMin(double minutes)
{
    if (!storeMinutes("landMin", minutes))
        return false;
    emit motorTableChanged();
    return true;
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

double VehicleProfileManager::watchedParam(const QString &name, double fallback) const
{
    return m_telemetry && m_telemetry->hasParameter(name) ? static_cast<double>(m_telemetry->parameterValue(name))
                                                          : fallback;
}

double VehicleProfileManager::anyParam(const QString &name) const
{
    return m_telemetry && m_telemetry->hasParameter(name) ? static_cast<double>(m_telemetry->parameterValue(name))
                                                          : copterParam(name);
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
        c.thresholdUs = releaseThresholdUs(c.output);
    return c;
}

double VehicleProfileManager::releaseThresholdUs(int output) const
{
    return PayloadRelease::threshold(copterParam(QStringLiteral("SERVO%1_MIN").arg(output)),
                                     copterParam(QStringLiteral("SERVO%1_MAX").arg(output)));
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
    const double mid = releaseThresholdUs(output);
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
        _noteDrop(n, c.eachKg(), QStringLiteral("output"));
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
    _noteDrop(n, c.eachKg(), QStringLiteral("tap"));
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
    return missionEstimateFrom(nullptr);
}

// Before take-off (live = nullptr) the mission is walked from home on the
// ground, on the charge the pack has from its resting voltage. In flight it is
// walked from where the copter is now: its position and height, the item it is
// flying to, already at speed when moving, on the charge left now and with the
// payload still aboard (only drops still ahead come off). In RTL only the
// return is left, in LAND only the descent; with the pilot flying (any other
// mode) the rest of the mission is shown as it would continue, marked
// "paused". RETURN NOW: the next waypoint is past the turn-back point, so
// going on to it means the pessimistic return lands below the reserve.
QVariantMap VehicleProfileManager::missionEstimateFrom(const LiveStart *live)
{
    QVariantMap out;
    // Already going home/down: RTL, LAND, or AUTO flying a return/land item.
    const bool returning = live && isReturning(live->mode, live->currentCommand);
    out[QStringLiteral("live")] = live != nullptr;
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
    // No mission is the usual case in manual flight: the item says so
    // without a warning (MissionEstimateCheck).
    if (!mm || mm->missionItems().size() < 2) {
        out[QStringLiteral("noMission")] = true;
        // Before arming a bad GPS still matters: home is set where the
        // copter is when it arms. Set it here too; the check further down is
        // only reached when there is a mission to cost.
        if (!live)
            out[QStringLiteral("gpsPoor")] = m_telemetry && m_telemetry->gpsPoor();
        return notReady(QStringLiteral("No mission on the copter"));
    }
    const double homeLat = m_telemetry->homeLatitude();
    const double homeLon = m_telemetry->homeLongitude();
    if (qAbs(homeLat) < 1e-6 || qAbs(homeLon) < 1e-6)
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
    MissionWalk::Start start;
    if (live) {
        start.lat = live->lat;
        start.lon = live->lon;
        start.altM = live->altM;
        start.moving = live->moving;
        start.nextSeq = live->nextSeq;
        // In RTL or LAND what is left is the return or the descent, from here.
        if (live->mode == QLatin1String("rtl") || live->mode == QLatin1String("land")) {
            MissionWalk::Item home = items.first();
            MissionWalk::Item last;
            last.seq = 1;
            last.command = live->mode == QLatin1String("rtl") ? MissionWalk::NavReturnToLaunch : MissionWalk::NavLand;
            items = {home, last};
            start.nextSeq = 1;
        }
        bool found = false;
        for (const MissionWalk::Item &it : items)
            found = found || it.seq == start.nextSeq;
        if (!found || start.nextSeq < 1)
            start.nextSeq = 1;
        out[QStringLiteral("mode")] = live->mode;
        out[QStringLiteral("returning")] = returning;
        out[QStringLiteral("returningLand")] = returning
            && (live->mode == QLatin1String("land") || live->currentCommand == MissionWalk::NavLand);
        out[QStringLiteral("nextSeq")] = start.nextSeq;
    }
    const MissionWalk::Settings ws = MissionWalk::Settings::read(copterParams(m_telemetry));
    const MissionWalk::Result w = MissionWalk::walk(items, ws, homeLat, homeLon, start, homeAmsl);

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
    in.payloadKg = live ? live->payloadKg : payloadKg;
    // The charge to start from. In flight: the charge left now, 0 % included
    // (an empty pack is empty, not unknown). Before take-off: the pack's
    // charge from its resting voltage; once armed (the take-off prediction
    // is made at the arm) the resting voltage read just before the arm, as
    // the idling motors already pull the voltage down a little.
    const double socNow = live ? live->socPct
                          : (m_telemetry->armed() && m_armVoltsKnown) ? m_armStartSocPct
                                                                      : packRestingSocPct(m_telemetry->batteryVoltage());
    in.startSocPct = std::clamp(socNow, 0.0, 100.0);
    in.reserveSocPct = m_batteryReserve * 100.0;
    const QString uid = m_currentDeviceUid;
    in.measuredRatio = [uid](double air) { return storedCruiseRatio(uid, air); };
    // The drag constant learned from the lean: until the cruise power of a
    // speed band is measured, it gives that band's extra power for the
    // leg's airspeed (head and tail winds then count).
    in.dragK = m_windK > 0.0 ? m_windK
                             : QSettings().value(QStringLiteral("WindFromLean/k/%1").arg(m_currentDeviceUid), 0.0).toDouble();
    in.currentLimitA = table->packCRating * capacityAh();
    in.battLowV = anyParam(QStringLiteral("BATT_LOW_VOLT"));
    in.battCrtV = anyParam(QStringLiteral("BATT_CRT_VOLT"));

    // Where the payload comes off: the mission's own release commands on the
    // release output (or a gripper release), in order, one payload each;
    // otherwise the item the operator picked for an RC drop (all of it there);
    // otherwise it is carried home (the safe case).
    QString dropText;
    if (in.payloadKg > 0.0 && w.ok) {
        int found = 0;
        // In flight, the payloads already released are not dropped again.
        const int left = drop.payloads - (live ? m_drop.released() : 0);
        for (const MissionWalk::Event &e : w.events) {
            const bool servo = (e.command == MissionWalk::DoSetServo || e.command == MissionWalk::DoRepeatServo)
                               && drop.output > 0 && qRound(e.p1) == drop.output
                               && (drop.onWhenLow ? e.p2 < drop.thresholdUs : e.p2 > drop.thresholdUs);
            const bool gripper = e.command == MissionWalk::DoGripper && qRound(e.p2) == 0;
            if ((servo || gripper) && found < left) {
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
                    in.drops.append({a.timeS, in.payloadKg});
            dropText = in.drops.isEmpty()
                           ? (item > 0 ? QStringLiteral("Drop item %1 is not a waypoint of this mission: payload costed as carried home").arg(item)
                                       : QStringLiteral("No drop point given: payload costed as carried home"))
                           : QStringLiteral("Dropped by the pilot at item %1").arg(item);
        }
    } else if (in.payloadKg <= 0.0) {
        dropText = live && payloadKg > 0.0 ? QStringLiteral("Payload released")
                   : m_payloadEntered ? QStringLiteral("No payload") : QStringLiteral("Payload not entered");
    }
    out[QStringLiteral("dropText")] = dropText;

    // The weather at the highest point of the mission.
    double topM = live ? live->altM : 0.0;
    for (const MissionWalk::Arrival &a : w.arrivals)
        topM = std::max(topM, a.altM);
    MissionAssess::Context ctx;
    ctx.inFlight = live != nullptr;
    ctx.flightHeightM = topM;
    ctx.hasDrops = !in.drops.isEmpty();
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
    // In flight, a wind measured from the copter's lean replaces the forecast
    // wind (the gust stays the forecast's).
    out[QStringLiteral("windSource")] = ctx.weatherKnown ? QStringLiteral("forecast") : QString();
    if (live && std::isfinite(live->windN) && std::isfinite(live->windE)) {
        WindFromLean::Fit wf;
        wf.windN = live->windN;
        wf.windE = live->windE;
        in.wind.speedMs = wf.speedMs();
        in.wind.fromDeg = wf.fromDeg();
        if (std::isfinite(in.wind.gustMs))
            in.wind.gustMs = std::max(in.wind.gustMs, in.wind.speedMs);
        ctx.weatherKnown = true;
        ctx.weatherConfidence = std::max(ctx.weatherConfidence, int(SiteWeather::Medium));
        weatherText = QStringLiteral("Wind %1 m/s from %2\u00B0 at %3 m (estimated from the lean)")
                          .arg(wf.speedMs(), 0, 'f', 1).arg(qRound(wf.fromDeg())).arg(qRound(live->altM));
        out[QStringLiteral("windSource")] = QStringLiteral("lean");
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
    lim.battLowAction = int(anyParam(QStringLiteral("BATT_FS_LOW_ACT")));
    lim.battCrtAction = int(anyParam(QStringLiteral("BATT_FS_CRT_ACT")));
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
    out[QStringLiteral("weatherRed")] = a.weatherRed;
    out[QStringLiteral("overCurrent")] = a.overCurrent;

    // Terrain along the mission (TerrainClearance.h). Before take-off the
    // legs are worked out from home and their terrain asked for; in flight
    // the legs not yet flown are checked from those (not in RTL or LAND, or
    // AUTO flying a return/land item, where the walk is the return from here).
    // A leg closer to the ground
    // than the margin goes first in the reasons and turns the item red.
    QStringList reasons = a.reasons;
    if (!live && w.ok) {
        m_terrainLegs = TerrainClearance::legsFrom(w, homeLat, homeLon, 0.0);
        requestTerrain(m_terrainLegs);
    }
    QVector<TerrainClearance::Leg> legs;
    if (w.ok && !returning) {
        for (const TerrainClearance::Leg &l : m_terrainLegs)
            if (!live || l.seq < 0 || l.seq >= live->nextSeq)
                legs.append(l);
    }
    const double groundAtHome = TerrainClearance::groundAtHome(m_terrainLegs, m_terrainProfiles, homeLat, homeLon);
    const TerrainClearance::Result tc = TerrainClearance::check(legs, m_terrainProfiles, groundAtHome);
    QStringList terrainReasons;
    QVariantList lows;
    for (const TerrainClearance::Low &low : tc.lows) {
        const QString where = low.seq >= 0 ? QStringLiteral("Item %1").arg(low.seq) : QStringLiteral("The return home");
        terrainReasons << QStringLiteral("%1: only %2 m above the ground, %3 m along the leg (needs %4 m)")
                              .arg(where).arg(low.clearanceM, 0, 'f', 0).arg(low.alongM, 0, 'f', 0)
                              .arg(TerrainClearance::kMarginM, 0, 'f', 0);
        lows.append(QVariantMap{{QStringLiteral("seq"), low.seq}, {QStringLiteral("clearanceM"), low.clearanceM},
                                {QStringLiteral("alongM"), low.alongM}, {QStringLiteral("lat"), low.lat},
                                {QStringLiteral("lon"), low.lon}});
    }
    if (!legs.isEmpty() && !tc.complete()) {
        bool loading = false;
        for (const TerrainClearance::Leg &l : legs)
            loading = loading || m_terrainPending.contains(l.key());
        terrainReasons << (loading ? QStringLiteral("Terrain along the mission not checked yet: terrain data loading")
                                   : QStringLiteral("Terrain along the mission not checked: no terrain data (%1 of %2 legs)")
                                         .arg(tc.legsKnown).arg(tc.legs));
    }
    // Before take-off, what the terrain check stands at is logged each time
    // it changes (the estimate is made every few seconds; the log says once).
    if (!live && w.ok) {
        QString summary = QStringLiteral("terrain along the mission: %1 of %2 legs").arg(tc.legsKnown).arg(tc.legs);
        if (!tc.homeKnown)
            summary += QStringLiteral(", home's ground not known: not checked");
        else if (std::isfinite(tc.lowestM))
            summary += QStringLiteral(", lowest %1 m above the ground").arg(tc.lowestM, 0, 'f', 0);
        if (!tc.lows.isEmpty()) {
            const TerrainClearance::Low &low = tc.lows.first();
            summary += low.seq >= 0 ? QStringLiteral(" (item %1)").arg(low.seq) : QStringLiteral(" (the return home)");
        }
        if (summary != m_terrainLogged) {
            m_terrainLogged = summary;
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: ") + summary;
        }
    }
    out[QStringLiteral("terrainRed")] = !tc.lows.isEmpty();
    out[QStringLiteral("terrainChecked")] = tc.complete();
    out[QStringLiteral("terrainLowestM")] = tc.lowestM;
    out[QStringLiteral("terrain")] = lows;
    out[QStringLiteral("reasons")] = terrainReasons + reasons;
    QStringList assumed;
    if (!ctx.cruiseMeasured)
        assumed << QStringLiteral("cruise power (hover in the likely case, %1× in the pessimistic case)")
                       .arg(MissionCost::kUnmeasuredPessimisticRatio, 0, 'f', 1);
    assumed << QStringLiteral("climb: half of the extra power lost");
    if (w.returnAssumed)
        assumed << QStringLiteral("RTL from the mission's last point");
    if (in.drops.isEmpty() && in.payloadKg > 0.0)
        assumed << QStringLiteral("payload carried home");
    out[QStringLiteral("assumed")] = assumed;

    // In flight: the time to the next waypoint, and RETURN NOW when the next
    // waypoint is already past the turn-back point (cost.turnBackSeq -1: not
    // even the first one ahead leaves a safe return). Not in RTL, LAND, or
    // AUTO flying a return/land item: already on the way down.
    if (live) {
        out[QStringLiteral("timeToNextS")] = w.arrivals.isEmpty() ? -1.0 : w.arrivals.first().timeS;
        out[QStringLiteral("turnBackSeq")] = cost.ok ? cost.turnBackSeq : -2;
        // The margin: how far the pessimistic return from the next waypoint
        // lands above the reserve (below 0 when it cannot return from there:
        // that waypoint is past the turn-back point). Held with hysteresis
        // (returnNowHeld) so that it does not flicker.
        double marginPct = qQNaN();
        int nextSeq = -1;
        if (cost.ok && !cost.arrivals.isEmpty()) {
            const MissionCost::ArrivalCost &first = cost.arrivals.first();
            nextSeq = first.seq;
            marginPct = !first.canReturn ? std::min(-1.0, first.returnLandPessimisticPct - in.reserveSocPct)
                                         : first.returnLandPessimisticPct - in.reserveSocPct;
        }
        // While the hover hold suppresses the displayed RETURN NOW, the
        // hysteresis must still see the unheld decision: once the model is
        // learned the red hint can come back without waiting for another
        // margin crossing.
        const bool wasOn = live ? m_missionReturnNowRaw
                                : m_missionLive.value(QStringLiteral("returnNow")).toBool();
        const bool now = returnNowHeld(wasOn, marginPct, returning);
        out[QStringLiteral("returnNow")] = now;
        const int pastSeq = cost.firstPastSeq >= 0 ? cost.firstPastSeq : nextSeq;
        out[QStringLiteral("returnNowWhy")] =
            !now ? QString()
            : pastSeq >= 0 ? QStringLiteral("Going on to item %1 leaves too little for the return: return now").arg(pastSeq)
                           : QStringLiteral("Too little left for the return: return now");
    }

    // Before arming: ArduPilot sets home where the copter is when it arms,
    // so an unreliable GPS now puts home in the wrong place.
    if (!live)
        out[QStringLiteral("gpsPoor")] = m_telemetry && m_telemetry->gpsPoor();

    QVariantList wps;
    for (const MissionAssess::Waypoint &p : a.waypoints) {
        int command = MissionWalk::NavReturnToLaunch;
        for (const MissionWalk::Arrival &ar : w.arrivals)
            if (ar.seq == p.seq && qAbs(ar.timeS - p.timeS) < 1e-6)
                command = ar.command;
        QVariantMap m;
        m[QStringLiteral("seq")] = p.seq;
        m[QStringLiteral("what")] = commandText(command, p.seq);
        m[QStringLiteral("timeText")] = live ? QStringLiteral("in ") + clockText(p.timeS) : clockText(p.timeS);
        m[QStringLiteral("timeS")] = p.timeS;
        for (const MissionWalk::Arrival &ar : w.arrivals)
            if (ar.seq == p.seq && qAbs(ar.timeS - p.timeS) < 1e-6) {
                m[QStringLiteral("lat")] = ar.lat;
                m[QStringLiteral("lon")] = ar.lon;
            }
        m[QStringLiteral("socLikelyPct")] = p.socLikelyPct;
        m[QStringLiteral("socPessimisticPct")] = p.socPessimisticPct;
        m[QStringLiteral("state")] = p.state;
        m[QStringLiteral("stateName")] = MissionAssess::stateName(p.state);
        wps.append(m);
    }
    out[QStringLiteral("waypoints")] = wps;
    return out;
}

// The in-flight estimate, every 2 s while armed and airborne (see
// missionEstimateFrom()). The charge now is the pack's own count (the FC's mAh
// against the selected pack, as the battery chip shows it).
void VehicleProfileManager::_updateMissionLive(int socPct, bool chargeUnknown)
{
    m_lastMissionSocPct = chargeUnknown ? -1 : socPct;
    QVariantMap next;
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    const double alt = m_telemetry ? m_telemetry->altitudeRelative() : qQNaN();
    const bool airborne = m_telemetry && m_telemetry->armed() && std::isfinite(alt) && alt > PowerTypeModel::kAirborneAltM;
    if (chargeUnknown) {
        // The FC's % is 100 with a dead sensor: it is not a charge reading.
        // Say so rather than cost a mission against a full pack that was not
        // there. No RETURN NOW is raised either (advice only, never an arm).
        next = missionLiveChargeUnknown();
    } else if (v && airborne && MotorTable::byId(m_motorTableId) && socPct >= 0) {
        LiveStart s;
        const QGeoCoordinate pos = v->coordinate();
        s.lat = pos.latitude();
        s.lon = pos.longitude();
        s.altM = alt;
        s.moving = m_telemetry->groundSpeed() > 1.0;
        s.socPct = socPct;
        s.payloadKg = livePayloadAboardKg();
        MissionManager *mm = v->missionManager();
        s.nextSeq = mm ? std::max(1, mm->currentIndex()) : 1;
        s.currentCommand = currentMissionCommand(mm);
        s.mode = liveModeOf(m_telemetry->flightMode());
        _windFromLean(&s.windN, &s.windE);
        // An item is reached when the autopilot moves on to the next one:
        // its real time since take-off and the charge then, next to the
        // take-off prediction.
        const int idx = mm ? mm->currentIndex() : -1;
        if (m_lastMissionIndex >= 1 && idx > m_lastMissionIndex) {
            for (int seq = m_lastMissionIndex; seq < idx; ++seq) {
                // Only navigation items are "reached": a servo, speed or other
                // DO command between them is passed over, not arrived at.
                int command = -1;
                for (const MissionItem *mi : mm->missionItems())
                    if (mi->sequenceNumber() == seq)
                        command = int(mi->command());
                if (!MissionWalk::isNavCommand(command))
                    continue;
                QVariantMap p;
                p[QStringLiteral("seq")] = seq;
                const double t = (QDateTime::currentMSecsSinceEpoch() - m_flightStartMs) / 1000.0;
                p[QStringLiteral("timeText")] = clockText(t);
                p[QStringLiteral("timeS")] = t;
                p[QStringLiteral("socPct")] = socPct;
                const auto pred = takeoffPredictionFor(m_takeoffEstimate, seq);
                p[QStringLiteral("predictedTimeText")] = pred.first >= 0.0 ? clockText(pred.first) : QString();
                p[QStringLiteral("predictedSocPct")] = pred.second;
                m_passed.append(p);
            }
        }
        if (idx >= 1)
            m_lastMissionIndex = idx;
        if (pos.isValid())
            next = withGpsPoor(missionEstimateFrom(&s), m_telemetry->gpsPoor());
        next[QStringLiteral("passed")] = m_passed;

        // The take-off hover model is conservative: it can raise RETURN NOW
        // ~16 s into a flight and clear it ~48 s later as the first hover
        // samples refine it. Hold the mission advice until this flight's
        // hover is actually used (15 samples), with a hard cap after the arm
        // for climbs that never hover. Only when take-off said it fits: a red
        // or absent estimate must show at once. Advice only, never an arm or
        // a flight change. returnNowWhy stays empty while held: the battery
        // panel shows it.
        const qint64 sinceArmMs = m_flightStartMs > 0
            ? QDateTime::currentMSecsSinceEpoch() - m_flightStartMs : -1;
        m_missionReturnNowRaw = next.value(QStringLiteral("returnNow")).toBool();
        const bool holding = holdMissionReturnNow(m_takeoffEstimate, m_hoverSamples, sinceArmMs);
        if (holding) {
            if (!m_missionReturnHeld)
                qInfo().noquote() << QStringLiteral("VehicleProfileManager: mission RETURN NOW held: waiting for this flight's hover (cap %1 s)")
                                         .arg(kHoverHoldMaxMs / 1000);
            next[QStringLiteral("returnNow")] = false;
            next[QStringLiteral("returnNowWhy")] = QString();
        } else if (m_missionReturnHeld) {
            const QString why = m_hoverSamples >= MotorTable::kMinHoverSamples
                ? QStringLiteral("hover learned")
                : sinceArmMs >= kHoverHoldMaxMs ? QStringLiteral("cap reached")
                : !takeoffEstimateFits(m_takeoffEstimate) ? QStringLiteral("take-off estimate said it does not fit")
                : QStringLiteral("hold ended");
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: mission RETURN NOW hold ended: %1").arg(why);
        }
        m_missionReturnHeld = holding;
        checkMissionAlerts(next);
    } else if (m_telemetry && !m_telemetry->armed()) {
        m_lastMissionIndex = -1;
    }
    // The 2 s live timer re-evaluates even with no messages. A link that goes
    // quiet in flight freezes these numbers: mark them stale after 5 s (the
    // battery chip's rule) and drop RETURN NOW, which must not stay red on
    // figures that are no longer live. isConnected() is deliberately untouched
    // — connect/disconnect still means a vehicle was added or removed.
    if (m_telemetry && m_telemetry->isConnected()
            && m_telemetry->streamAgeMs(TelemetryStream::Heartbeat) > 5000) {
        next[QStringLiteral("stale")] = true;
        next[QStringLiteral("returnNow")] = false;
    }
    if (next != m_missionLive) {
        const bool was = m_missionLive.value(QStringLiteral("returnNow")).toBool();
        m_missionLive = next;
        if (was != next.value(QStringLiteral("returnNow")).toBool())
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: mission RETURN NOW %1%2")
                                     .arg(next.value(QStringLiteral("returnNow")).toBool() ? QStringLiteral("on") : QStringLiteral("off"))
                                     .arg(next.value(QStringLiteral("returnNowWhy")).toString().isEmpty()
                                              ? QString() : QStringLiteral(": ") + next.value(QStringLiteral("returnNowWhy")).toString());
        emit liveDataChanged();
    }
}

QPair<double, double> VehicleProfileManager::takeoffPredictionFor(const QVariantMap &takeoffEstimate, int seq)
{
    for (const QVariant &wv : takeoffEstimate.value(QStringLiteral("waypoints")).toList()) {
        const QVariantMap wp = wv.toMap();
        if (wp.value(QStringLiteral("seq")).toInt() == seq)
            return {wp.value(QStringLiteral("timeS")).toDouble(), wp.value(QStringLiteral("socPessimisticPct")).toDouble()};
    }
    return {-1.0, -1.0};
}

QString VehicleProfileManager::liveModeOf(const QString &flightMode)
{
    const QString mode = flightMode.trimmed().toLower();
    if (mode == QLatin1String("auto"))
        return QStringLiteral("auto");
    if (mode.contains(QLatin1String("rtl")))
        return QStringLiteral("rtl");
    if (mode == QLatin1String("land"))
        return QStringLiteral("land");
    return QStringLiteral("paused");
}

// Alerts in flight, once each: the result getting worse than it has been
// this flight (e.g. Fits -> Tight), RETURN NOW coming on (the next waypoint
// is past the turn-back point), and the weather in use being old (once per
// flight).
void VehicleProfileManager::checkMissionAlerts(const QVariantMap &next)
{
    if (!next.value(QStringLiteral("ready")).toBool())
        return;
    // The result: said only when it is worse than any result before it this
    // flight (m_lastAlertResult keeps the worst), so one that goes back and
    // forth between two levels is said once. Can't assess is passed over.
    const int result = next.value(QStringLiteral("result")).toInt();
    if (result != int(MissionAssess::CantAssess)) {
        if (m_lastAlertResult >= 0 && result < m_lastAlertResult)
            emit missionAlert(QStringLiteral("Mission now %1").arg(next.value(QStringLiteral("resultName")).toString()), true);
        if (m_lastAlertResult < 0 || result < m_lastAlertResult)
            m_lastAlertResult = result;
    }
    if (next.value(QStringLiteral("returnNow")).toBool() && !m_missionLive.value(QStringLiteral("returnNow")).toBool())
        emit missionAlert(QStringLiteral("Return now: %1").arg(next.value(QStringLiteral("returnNowWhy")).toString()), true);
    if (!m_alertedOldWeather) {
        for (const QVariant &r : next.value(QStringLiteral("reasons")).toList())
            if (r.toString().startsWith(QLatin1String("Old weather"))) {
                m_alertedOldWeather = true;
                emit missionAlert(r.toString(), false);
                break;
            }
    }
}

// Every 2 s in flight (see WindFromLean.h): a steady moment (level, not
// speeding up) adds a sample with the weight aboard; the wind then comes from
// the last minute with this copter's drag constant, or, with none saved yet,
// from learning the constant on this flight once it has turned enough.
// While the GPS is unreliable its velocity is too: no sample is added, so
// nothing is learned from it (a wrong constant would be kept for later
// flights), and the wind comes from the samples already kept.
bool VehicleProfileManager::_windFromLean(double *windN, double *windE)
{
    if (!m_telemetry)
        return false;
    const double vN = m_telemetry->velocityNorthMs();
    const double vE = m_telemetry->velocityEastMs();
    const double emptyKg = emptyWeightKg();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!std::isfinite(vN) || !std::isfinite(vE) || !(emptyKg > 0.0))
        return false;
    const bool gpsPoor = m_telemetry->gpsPoor();
    if (gpsPoor) {
        // The next good velocity is not compared with this one.
        m_windLastN = m_windLastE = std::numeric_limits<double>::quiet_NaN();
        m_windLastMs = 0;
    }
    const double dt = m_windLastMs > 0 ? (now - m_windLastMs) / 1000.0 : 0.0;
    const double accel = dt > 0.1 && std::isfinite(m_windLastN)
                             ? std::hypot(vN - m_windLastN, vE - m_windLastE) / dt : 1e9;
    if (!gpsPoor) {
        m_windLastN = vN;
        m_windLastE = vE;
        m_windLastMs = now;
    }
    if (!gpsPoor && WindFromLean::isSteady(m_telemetry->verticalSpeed(), accel)) {
        WindFromLean::Sample smp;
        smp.rollRad = m_telemetry->roll();
        smp.pitchRad = m_telemetry->pitch();
        smp.yawRad = m_telemetry->yaw();
        smp.velN = vN;
        smp.velE = vE;
        smp.massKg = emptyKg + livePayloadAboardKg();
        m_windSamples.append(smp);
    }
    WindFromLean::Fit fit;
    if (m_windK > 0.0) {
        constexpr int kLastMinute = 30;   // samples every 2 s
        fit = WindFromLean::wind(m_windSamples.mid(std::max(0, int(m_windSamples.size()) - kLastMinute)), m_windK);
    } else if (m_windSamples.size() >= WindFromLean::kMinSamples && m_windSamples.size() % 5 == 0) {
        fit = WindFromLean::learn(m_windSamples);
        if (fit.ok) {
            m_windK = fit.k;
            m_windKLearnedThisFlight = true;
            qInfo().noquote() << QStringLiteral("VehicleProfileManager: drag constant learned from the lean: %1 (rms %2)")
                                     .arg(fit.k, 0, 'f', 3).arg(fit.rmsTan, 0, 'f', 3);
        }
    }
    if (!fit.ok) {
        m_windEstN = m_windEstE = std::numeric_limits<double>::quiet_NaN();
        return false;
    }
    *windN = m_windEstN = fit.windN;
    *windE = m_windEstE = fit.windE;
    return true;
}

// A payload released this flight, for the flight's mission record.
void VehicleProfileManager::_noteDrop(int index, double kg, const QString &how)
{
    QVariantMap d;
    d[QStringLiteral("index")] = index;
    d[QStringLiteral("kg")] = kg;
    d[QStringLiteral("timeS")] = m_flightStartMs > 0 ? (QDateTime::currentMSecsSinceEpoch() - m_flightStartMs) / 1000.0 : -1.0;
    d[QStringLiteral("how")] = how;
    m_dropEvents.append(d);
}

QVariantMap VehicleProfileManager::missionAccuracy() const
{
    QVector<MissionRecord::Record> recs;
    if (!m_motorTableId.isEmpty())
        for (const QVariant &v : DatabaseManager::instance().motorTableFlights(m_currentDeviceUid, m_motorTableId))
            recs.append(MissionRecord::fromJson(v.toMap().value(QStringLiteral("missionJson")).toString()));
    const MissionRecord::Accuracy a = MissionRecord::accuracy(recs);
    QVariantMap m;
    m[QStringLiteral("flights")] = a.flights;
    m[QStringLiteral("meanAbsSocPts")] = a.meanAbsSocPts;
    m[QStringLiteral("meanAbsTimePct")] = a.meanAbsTimePct;
    m[QStringLiteral("atOrAbovePessimistic")] = a.atOrAbovePessimistic;
    m[QStringLiteral("text")] =
        a.flights == 0 ? QStringLiteral("No flight with a mission estimate recorded yet.")
                       : QStringLiteral("Mission estimate over %1 flight%2: landing charge off by %3 points and flight "
                                        "time by %4 % on average; %5 of %1 landed with at least the pessimistic charge.")
                             .arg(a.flights).arg(a.flights > 1 ? "s" : "")
                             .arg(a.meanAbsSocPts, 0, 'f', 1).arg(a.meanAbsTimePct, 0, 'f', 0)
                             .arg(a.atOrAbovePessimistic);
    return m;
}

bool VehicleProfileManager::isReturning(const QString &flightMode, int currentCommand)
{
    const QString mode = liveModeOf(flightMode);
    if (mode == QLatin1String("rtl") || mode == QLatin1String("land"))
        return true;
    return mode == QLatin1String("auto")
           && (currentCommand == MissionWalk::NavReturnToLaunch || currentCommand == MissionWalk::NavLand);
}

double VehicleProfileManager::cruiseReferenceThrustG(double flightSum, int flightSamples, double setupThrustG,
                                                     const QString &setupSource)
{
    if (flightSamples >= MotorTable::kMinHoverSamples && flightSum > 0.0)
        return flightSum / flightSamples;
    if (setupSource == QLatin1String("last flight") && setupThrustG > 0.0)
        return setupThrustG;
    return -1.0;
}

// The terrain under each leg not yet known, asked for once at a time per leg
// (again after kTerrainRetryMs when it could not be had). The answer is kept
// by the leg's key; the mission estimate reads it on its next update.
//
// The query can answer at once, inside requestData() (a tile it has cached).
// So the legs are a copy (the estimate that asked may run again and replace
// m_terrainLegs), and terrainChanged() is sent from the event loop, never from
// inside here: the estimate is not re-run in the middle of this loop.
void VehicleProfileManager::requestTerrain(QVector<TerrainClearance::Leg> legs)
{
    if (!m_terrainQueriesEnabled)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int asked = 0;
    for (const TerrainClearance::Leg &l : legs) {
        const QString key = l.key();
        if (m_terrainProfiles.value(key).ok || m_terrainPending.contains(key))
            continue;
        if (m_terrainFailedMs.contains(key) && now - m_terrainFailedMs.value(key) < kTerrainRetryMs)
            continue;
        m_terrainPending.insert(key);
        ++asked;
        auto *q = new TerrainPathQuery(true /* deletes itself after answering */);
        connect(q, &TerrainPathQuery::terrainDataReceived, this,
                [this, key, leg = l](bool success, const TerrainPathQuery::PathHeightInfo_t &info) {
                    m_terrainPending.remove(key);
                    TerrainClearance::Profile p;
                    p.ok = success && !info.heights.isEmpty();
                    p.spacingM = info.distanceBetween;
                    p.finalSpacingM = info.finalDistanceBetween;
                    p.heights = QVector<double>(info.heights.cbegin(), info.heights.cend());
                    if (p.ok) {
                        keepTerrainProfile(key, p);
                    } else {
                        m_terrainFailedMs.insert(key, QDateTime::currentMSecsSinceEpoch());
                        qInfo().noquote() << QStringLiteral("VehicleProfileManager: terrain under a mission leg could not "
                                                            "be had (asked again in %1 s)").arg(kTerrainRetryMs / 1000);
                        // Asked again by itself, not only when the estimate
                        // next runs; only while the leg is still the mission's.
                        QTimer::singleShot(kTerrainRetryMs + 100, this, [this, leg]() {
                            for (const TerrainClearance::Leg &cur : m_terrainLegs)
                                if (cur.key() == leg.key()) {
                                    requestTerrain({leg});
                                    return;
                                }
                        });
                    }
                    QMetaObject::invokeMethod(this, [this]() { emit terrainChanged(); }, Qt::QueuedConnection);
                });
        q->requestData(QGeoCoordinate(l.fromLat, l.fromLon), QGeoCoordinate(l.toLat, l.toLon));
    }
    if (asked > 0)
        qInfo().noquote() << QStringLiteral("VehicleProfileManager: asking for the terrain under %1 mission leg(s)").arg(asked);
}

bool VehicleProfileManager::keepTerrainProfile(const QString &key, const TerrainClearance::Profile &profile)
{
    if (m_terrainProfiles.value(key).ok)
        return false;   // kept: a later answer does not replace it
    m_terrainProfiles.insert(key, profile);
    m_terrainFailedMs.remove(key);
    return true;
}

void VehicleProfileManager::setTerrainProfile(const QString &key, const TerrainClearance::Profile &profile)
{
    if (keepTerrainProfile(key, profile))
        emit terrainChanged();
}

VehicleProfileManager::FcCurrentCheck VehicleProfileManager::fcCurrentCheck(FcCurrentCheck s, bool airborne,
                                                                           double tableA, double fcA, double dtS)
{
    if (!airborne || !(tableA >= kFcCurrentMinTableA) || !std::isfinite(fcA) || fcA < 0.0 || !(dtS > 0.0)) {
        s.deadS = s.aliveS = 0.0;   // no evidence: the run starts again
        return s;
    }
    if (fcA < std::max(kFcCurrentDeadA, kFcCurrentDeadFraction * tableA)) {
        s.aliveS = 0.0;
        s.deadS += dtS;
        if (s.deadS >= kFcCurrentWindowS)
            s.dead = true;
    } else if (fcA >= kFcCurrentAliveFraction * tableA) {
        s.deadS = 0.0;
        s.aliveS += dtS;
        if (s.aliveS >= kFcCurrentWindowS)
            s.dead = false;
    } else {
        s.deadS = s.aliveS = 0.0;
    }
    return s;
}

void VehicleProfileManager::setFcCurrentDead(bool dead)
{
    if (!m_currentDeviceUid.isEmpty()) {
        const QString key = QStringLiteral("MotorTable/fcCurrentDead/%1").arg(m_currentDeviceUid);
        if (dead)
            QSettings().setValue(key, true);
        else
            QSettings().remove(key);
    }
    qInfo().noquote() << (dead ? QStringLiteral("VehicleProfileManager: the flight controller's current reads about 0 A in "
                                                "flight: the GCS counts the charge itself")
                               : QStringLiteral("VehicleProfileManager: the flight controller's current is measured again"));
}

QVariantMap VehicleProfileManager::withGpsPoor(QVariantMap live, bool gpsPoor)
{
    if (!gpsPoor)
        return live;
    live[QStringLiteral("gpsPoor")] = true;
    QStringList reasons = live.value(QStringLiteral("reasons")).toStringList();
    reasons.prepend(kGpsPoorReason);
    live[QStringLiteral("reasons")] = reasons;
    if (live.value(QStringLiteral("returnNow")).toBool())
        live[QStringLiteral("returnNowWhy")] = live.value(QStringLiteral("returnNowWhy")).toString()
                                               + QStringLiteral(" (GPS unreliable: the distance home may be wrong)");
    return live;
}

bool VehicleProfileManager::returnNowHeld(bool wasOn, double marginPct, bool returning)
{
    if (returning)
        return false;
    if (!std::isfinite(marginPct))
        return wasOn;
    if (marginPct < 0.0)
        return true;
    return wasOn && marginPct < kReturnNowClearPts;
}

bool VehicleProfileManager::takeoffEstimateFits(const QVariantMap &estimate)
{
    if (!estimate.value(QStringLiteral("ready")).toBool())
        return false;
    const int result = estimate.value(QStringLiteral("result"), int(MissionAssess::CantAssess)).toInt();
    return result == MissionAssess::Fits || result == MissionAssess::Comfortable;
}

bool VehicleProfileManager::holdMissionReturnNow(const QVariantMap &takeoffEstimate, int hoverSamples, qint64 sinceArmMs)
{
    return takeoffEstimateFits(takeoffEstimate)
        && hoverSamples < MotorTable::kMinHoverSamples
        && sinceArmMs >= 0
        && sinceArmMs < kHoverHoldMaxMs;
}

QString VehicleProfileManager::returnNowOffWhy(const QVariantMap &powerLive, bool returning)
{
    if (powerLive.value(QStringLiteral("returnNowFrom")).toString() == QLatin1String("mission"))
        return QStringLiteral("from the mission estimate");
    if (powerLive.value(QStringLiteral("chargeUnknown")).toBool())
        return QStringLiteral("charge unknown (joined in flight)");
    if (!powerLive.value(QStringLiteral("armed")).toBool())
        return QStringLiteral("disarmed / flight ended");
    if (!powerLive.value(QStringLiteral("airborne")).toBool())
        return QStringLiteral("on the ground / flight ended");
    if (returning)
        return QStringLiteral("already returning");
    return QStringLiteral("trip fits again");
}

VehicleProfileManager::TripAdvice VehicleProfileManager::tripAdvice(bool tripShort, bool returning)
{
    TripAdvice a;
    a.returnNow = tripShort && !returning;
    a.landingOnReserve = tripShort && returning;
    return a;
}
