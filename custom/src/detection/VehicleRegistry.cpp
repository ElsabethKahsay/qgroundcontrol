#include "VehicleRegistry.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtCore/QCryptographicHash>
#include <QtCore/QApplicationStatic>
#include <QtCore/QLoggingCategory>

#include "Vehicle/Vehicle.h"
#include "MultiVehicleManager.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"
#include "utils/DatabaseManager.h"
#include "managers/FlightSession.h"
#include "HardwareTestController.h"

Q_APPLICATION_STATIC(VehicleRegistry, _vehicleRegistryInstance)

Q_LOGGING_CATEGORY(vehicleRegistryLog, "vehicle.registry")

VehicleRegistry *VehicleRegistry::instance()
{
    return _vehicleRegistryInstance();
}

VehicleRegistry::VehicleRegistry(QObject *parent)
    : QObject(parent)
{
    // Listen for vehicle connect/disconnect and active-vehicle changes from the global manager.
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::vehicleAdded,
            this, &VehicleRegistry::_onVehicleAdded);
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::vehicleRemoved,
            this, &VehicleRegistry::_onVehicleRemoved);
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
            this, &VehicleRegistry::_onActiveVehicleChanged);

    m_autopilotRequestTimer.setInterval(2000);
    m_autopilotRequestTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleTimeoutMs);
    m_settleTimer.setSingleShot(true);
    connect(&m_settleTimer, &QTimer::timeout, this, &VehicleRegistry::_onSettleTimeout);
    connect(&m_autopilotRequestTimer, &QTimer::timeout,
            this, &VehicleRegistry::_requestAutopilotCapabilities);

    // Pick up any vehicle already connected at construction time.
    Vehicle *activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (activeVehicle)
        _onVehicleAdded(activeVehicle);
}

bool VehicleRegistry::_hasValidUid(quint64 uid)
{
    return uid != 0;
}

QString VehicleRegistry::computeFingerprint(quint64 uid, bool hasUid, int mavType, int sysid, const QString &boardVersion)
{
    // Hardware-backed identity wherever possible; the sysid fallback keeps each
    // board distinct even when the autopilot never reports a UID.
    const QString data = hasUid
        ? QStringLiteral("uid=%1|type=%2|board=%3").arg(uid).arg(mavType).arg(boardVersion)
        : QStringLiteral("sysid=%1|type=%2|board=%3").arg(sysid).arg(mavType).arg(boardVersion);
    return QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex());
}

/// When a vehicle connects, request its AUTOPILOT_VERSION: it carries the
/// hardware ids (uid, uid2) and the board id. The vehicle is looked up or
/// registered only once that message is in (_settleIdentity), or after
/// kSettleTimeoutMs for a vehicle that never sends it.
void VehicleRegistry::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) return;
    // vehicleAdded and activeVehicleChanged both report the same vehicle.
    if (vehicle == m_currentVehicle && (m_identitySettled || m_settleTimer.isActive()))
        return;
    qCDebug(vehicleRegistryLog) << "Vehicle added: sysid" << vehicle->id();

    if (m_currentVehicle && m_currentVehicle != vehicle)
        disconnect(m_currentVehicle, nullptr, this, nullptr);

    m_currentVehicle = vehicle;
    m_pendingVehicle = nullptr;
    _resetIdentity();

    connect(vehicle, &Vehicle::vehicleUIDChanged,
            this, &VehicleRegistry::_onVehicleUidChanged, Qt::UniqueConnection);
    connect(vehicle, &Vehicle::firmwareVersionChanged,
            this, &VehicleRegistry::_onFirmwareVersionChanged, Qt::UniqueConnection);
    connect(vehicle, &Vehicle::mavlinkMessageReceived,
            this, &VehicleRegistry::_onMavlinkMessage, Qt::UniqueConnection);

    // Give the connection a moment to settle, then ask for capabilities.
    m_autopilotRequestTimer.start();
    m_settleTimer.start();

    _extractVehicleInfo(vehicle);
    if (_hasValidUid(m_currentUid))
        _settleIdentity();
}

/// QGC parsed AUTOPILOT_VERSION. A non-zero uid settles (or upgrades) the
/// identity; uid = 0 waits for the message itself, which also carries uid2.
void VehicleRegistry::_onVehicleUidChanged()
{
    if (!m_currentVehicle) return;
    qCDebug(vehicleRegistryLog) << "Vehicle UID now known:" << m_currentVehicle->vehicleUID();

    _extractVehicleInfo(m_currentVehicle);
    if (!_hasValidUid(m_currentUid))
        return;
    if (!m_identitySettled)
        _settleIdentity();
    else if (m_currentFingerprintSource != QStringLiteral("HARDWARE_UID"))
        _upgradeToHardwareIdentity(m_currentVehicle);
}

/// QGC parses AUTOPILOT_VERSION asynchronously (after our registration already
/// ran with an empty firmware string).  Re-extract when the firmware arrives so
/// the database never keeps an empty or stale version.
void VehicleRegistry::_onFirmwareVersionChanged()
{
    if (!m_currentVehicle) return;
    qCDebug(vehicleRegistryLog) << "Firmware version changed, refreshing attributes:"
                                << m_currentFirmwareVersion;

    _extractVehicleInfo(m_currentVehicle);
    if (m_identitySettled)
        _updateCurrentVehicleAttributes(m_currentVehicle);
}

/// Explicitly request the autopilot capabilities (AUTOPILOT_VERSION).  QGC's
/// initial-connection state machine usually does this already, but the explicit
/// request guarantees the UID arrives even when the standard handshake was
/// satisfied with cached data.
void VehicleRegistry::_requestAutopilotCapabilities()
{
    if (!m_currentVehicle) return;
    m_currentVehicle->sendMavCommand(m_currentVehicle->defaultComponentId(),
                                     MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES,
                                     false, 1.0f);
    qCDebug(vehicleRegistryLog) << "Requested AUTOPILOT_VERSION for sysid" << m_currentVehicle->id();
}

/// AUTOPILOT_VERSION: read uid, uid2 and the board id from the message itself
/// (the Vehicle may not have parsed it yet), then settle the identity, or
/// upgrade a fallback identity once a hardware id appears.
void VehicleRegistry::_onMavlinkMessage(const mavlink_message_t &message)
{
    if (message.msgid != MAVLINK_MSG_ID_AUTOPILOT_VERSION) return;
    if (!m_currentVehicle) return;
    if (message.sysid != static_cast<uint8_t>(m_currentVehicle->id())) return;

    mavlink_autopilot_version_t version{};
    mavlink_msg_autopilot_version_decode(&message, &version);

    qCDebug(vehicleRegistryLog) << "AUTOPILOT_VERSION received: uid=" << version.uid
                                << "uid2=" << QByteArray(reinterpret_cast<const char *>(version.uid2),
                                                         static_cast<int>(sizeof(version.uid2))).toHex()
                                << "board=" << version.board_version << "product=" << version.product_id;

    if (_hasValidUid(version.uid))
        m_currentUid = version.uid;
    const QString uid2 = uid2Key(version.uid2, static_cast<int>(sizeof(version.uid2)));
    if (!uid2.isEmpty())
        m_currentUid2Key = uid2;
    if (version.product_id != 0 || m_currentBoardVersion.isEmpty())
        m_currentBoardVersion = QString::number(version.product_id);

    if (!m_identitySettled) {
        _settleIdentity();
    } else if (m_currentFingerprintSource != QStringLiteral("HARDWARE_UID")
               && (_hasValidUid(m_currentUid) || !m_currentUid2Key.isEmpty())) {
        _upgradeToHardwareIdentity(m_currentVehicle);
    }
}

/// A hardware id arrived after the identity was settled on the fallback (a late
/// or retried AUTOPILOT_VERSION). If this connection created the fallback row,
/// the row is renamed to the hardware identity (its records follow). A fallback
/// row that existed before is left alone: it may be another copter's with the
/// same sysid, frame and board model, or the simulator's. The vehicle then
/// takes its own hardware row.
void VehicleRegistry::_upgradeToHardwareIdentity(Vehicle *vehicle)
{
    if (!vehicle) return;
    const QString oldFingerprint = m_currentFingerprint;
    const QString oldKey = currentDeviceKey();
    _computeIdentity();
    if (m_currentFingerprintSource != QStringLiteral("HARDWARE_UID") || oldFingerprint == m_currentFingerprint)
        return;
    const QString newKey = currentDeviceKey();
    qCDebug(vehicleRegistryLog) << "Upgrading vehicle identity to hardware id" << newKey;

    if (m_registeredNewThisConnection && !oldFingerprint.isEmpty()) {
        if (DatabaseManager::instance().updateVehicleFingerprint(oldFingerprint, m_currentFingerprint,
                                                                 QStringLiteral("HARDWARE_UID"), newKey)) {
            qCDebug(vehicleRegistryLog) << "Re-keyed vehicle record to" << newKey;
            emit vehicleReKeyed(oldKey, newKey);
        }
        _updateCurrentVehicleAttributes(vehicle);
        emit knownVehicleChanged();
    } else {
        m_registeredNewThisConnection = false;
        _registerOrUpdateCurrentVehicle();
    }
    emit currentVehicleChanged(m_currentVehicleId);
    emit identitySettled();
}

void VehicleRegistry::_registerOrUpdateCurrentVehicle()
{
    if (m_currentFingerprint.isEmpty()) {
        qCWarning(vehicleRegistryLog) << "Cannot register vehicle without a fingerprint";
        return;
    }

    QString json = DatabaseManager::instance().lookupVehicleByFingerprint(m_currentFingerprint);
    if (!json.isEmpty()) {
        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        m_isKnownVehicle = true;
        m_vehicleName = obj.value(QStringLiteral("friendlyName")).toString();
        DatabaseManager::instance().updateVehicleLastSeen(m_currentFingerprint);

        // If firmware was updated since last connection, persist the new version.
        QString dbFw = obj.value(QStringLiteral("firmwareVersion")).toString();
        if (!m_currentFirmwareVersion.isEmpty() && dbFw != m_currentFirmwareVersion) {
            DatabaseManager::instance().updateVehicleFirmware(m_currentFingerprint, m_currentFirmwareVersion);
        }

        qCDebug(vehicleRegistryLog) << "Known vehicle connected:" << m_vehicleName;
        emit knownVehicleConnected(m_currentVehicleId);
    } else {
        m_isKnownVehicle = false;
        // Auto-generate a temporary name until the user assigns one.
        m_vehicleName = QStringLiteral("UAV-%1-%2")
                            .arg(m_currentVehicleId)
                            .arg(m_currentFingerprint.left(8));

        m_registeredNewThisConnection = true;
        DatabaseManager::instance().registerNewVehicle(
            m_currentFingerprint, m_currentVehicleId, m_currentCompid,
            autopilotTypeString(m_currentAutopilotType),
            vehicleTypeString(m_currentVehicleType),
            m_currentVehicleTypeName,
            m_currentFirmwareVersion, m_currentUid, m_currentHardwareUid,
            m_currentBoardVersion, m_vehicleName, m_currentFingerprintSource);

        qCDebug(vehicleRegistryLog) << "New vehicle registered:" << m_vehicleName
                                    << "source:" << m_currentFingerprintSource;
        emit newVehicleRegistered(m_currentVehicleId);
    }

    _updateCurrentVehicleAttributes(m_currentVehicle);
    emit knownVehicleChanged();
}

/// Refresh the live attribute columns (type, firmware, frame, motors) every
/// connect so the Vehicles page always reflects the current airframe details.
/// Motor count and frame class come from the same parameter logic the
/// HardwareTestController uses (PX4 CA_AIRFRAME / ArduPilot FRAME_CLASS), falling
/// back to the MAV_TYPE from HEARTBEAT.
void VehicleRegistry::_updateCurrentVehicleAttributes(Vehicle *vehicle)
{
    if (!vehicle) return;

    int frameClass = -1;
    int motorCount = m_currentMotorCount;

    auto *paramMgr = vehicle->parameterManager();
    int compId = vehicle->defaultComponentId();
    auto getParam = [paramMgr, compId](const QString &name, float fallback) -> float {
        if (!paramMgr || !paramMgr->parametersReady()) return fallback;
        Fact *fact = paramMgr->getParameter(compId, name);
        if (!fact) return fallback;
        return fact->rawValue().toFloat();
    };

    if (vehicle->px4Firmware()) {
        int caAirframe = static_cast<int>(getParam(QStringLiteral("CA_AIRFRAME"), -1));
        frameClass = caAirframe;
        int cnt = HardwareTestController::motorCountFromCaAirframe(caAirframe);
        if (cnt < 0 && caAirframe == 0)
            cnt = qBound(1, static_cast<int>(getParam(QStringLiteral("CA_ROTOR_CNT"), 4)), 16);
        if (cnt < 0)
            cnt = HardwareTestController::motorCountFromMavType(vehicle->vehicleType());
        motorCount = qMax(0, cnt);
    } else if (vehicle->apmFirmware()) {
        int fc = static_cast<int>(getParam(QStringLiteral("FRAME_CLASS"), -1));
        frameClass = fc;
        int cnt = HardwareTestController::motorCountFromFrameClass(fc);
        if (cnt < 0)
            cnt = HardwareTestController::motorCountFromMavType(vehicle->vehicleType());
        motorCount = qMax(0, cnt);
    } else {
        frameClass = -1;
        motorCount = HardwareTestController::motorCountFromMavType(vehicle->vehicleType());
    }

    m_currentMotorCount = motorCount;
    m_currentFrameClass = frameClass;
    m_currentVehicleTypeName = HardwareTestController::vehicleTypeLabelFromMavType(vehicle->vehicleType());

    DatabaseManager::instance().updateVehicleAttributes(
        m_currentFingerprint,
        autopilotTypeString(m_currentAutopilotType),
        vehicleTypeString(m_currentVehicleType),
        m_currentVehicleTypeName,
        m_currentFirmwareVersion,
        m_currentHardwareUid,
        m_currentVehicleId,
        m_currentBoardVersion,
        frameClass,
        motorCount);
}

void VehicleRegistry::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) return;
    qCDebug(vehicleRegistryLog) << "Vehicle removed: sysid" << vehicle->id();

    if (m_currentVehicle == vehicle) {
        disconnect(vehicle, nullptr, this, nullptr);
        m_currentVehicle = nullptr;
        m_pendingVehicle = nullptr;
    }

    FlightSession *fs = FlightSession::instance();
    if (fs) fs->closeSession();

    // Record the last-seen time before clearing state.
    if (!m_currentFingerprint.isEmpty()) {
        DatabaseManager::instance().updateVehicleLastSeen(m_currentFingerprint);
    }

    m_autopilotRequestTimer.stop();
    m_currentVehicleId = 0;
    _resetIdentity();
    m_isKnownVehicle = false;
    m_vehicleName.clear();
    emit currentVehicleChanged(0);
    emit knownVehicleChanged();
}

void VehicleRegistry::_onActiveVehicleChanged(Vehicle *vehicle)
{
    if (vehicle) {
        _onVehicleAdded(vehicle);
    } else {
        if (m_currentVehicle) {
            disconnect(m_currentVehicle, nullptr, this, nullptr);
            m_currentVehicle = nullptr;
        }
        m_autopilotRequestTimer.stop();
        m_currentVehicleId = 0;
        _resetIdentity();
        m_isKnownVehicle = false;
        m_vehicleName.clear();
        emit currentVehicleChanged(0);
        emit knownVehicleChanged();
    }
}

/// Pull the vehicle's attributes from the QGC Vehicle object. The identity
/// (fingerprint, source, key) is not touched here: it is fixed once per
/// connection by _settleIdentity(), so a board id or firmware version that
/// arrives later cannot change the key under the vehicle's records.
void VehicleRegistry::_extractVehicleInfo(Vehicle *vehicle)
{
    if (!vehicle) return;

    m_currentVehicleId = vehicle->id();
    m_currentCompid = vehicle->defaultComponentId();
    m_currentAutopilotType = static_cast<int>(vehicle->firmwareType());
    m_currentVehicleType = static_cast<int>(vehicle->vehicleType());
    m_currentVehicleTypeName = HardwareTestController::vehicleTypeLabelFromMavType(m_currentVehicleType);
    m_currentMotorCount = HardwareTestController::motorCountFromMavType(m_currentVehicleType);
    // The AUTOPILOT_VERSION handler may have read uid and the board id from the
    // message before the Vehicle did: never replace them with "not known yet".
    if (_hasValidUid(vehicle->vehicleUID()))
        m_currentUid = vehicle->vehicleUID();
    if (vehicle->firmwareBoardProductId() != 0 || m_currentBoardVersion.isEmpty())
        m_currentBoardVersion = QString::number(vehicle->firmwareBoardProductId());

    int majorVer = vehicle->firmwareMajorVersion();
    int minorVer = vehicle->firmwareMinorVersion();
    int patchVer = vehicle->firmwarePatchVersion();
    m_currentFirmwareVersion = (majorVer >= 0 && minorVer >= 0 && patchVer >= 0)
        ? QStringLiteral("%1.%2.%3").arg(majorVer).arg(minorVer).arg(patchVer)
        : QString();
}

void VehicleRegistry::_computeIdentity()
{
    if (_hasValidUid(m_currentUid)) {
        m_currentFingerprintSource = QStringLiteral("HARDWARE_UID");
        m_currentHardwareUid = QString("%1").arg(m_currentUid, 0, 16);
        m_currentFingerprint = computeFingerprint(m_currentUid, true, m_currentVehicleType,
                                                  m_currentVehicleId, m_currentBoardVersion);
    } else if (!m_currentUid2Key.isEmpty()) {
        m_currentFingerprintSource = QStringLiteral("HARDWARE_UID");
        m_currentHardwareUid = m_currentUid2Key;
        m_currentFingerprint = computeUid2Fingerprint(m_currentUid2Key, m_currentVehicleType);
    } else {
        m_currentFingerprintSource = QStringLiteral("SYSID_TYPE_FALLBACK");
        m_currentHardwareUid.clear();
        m_currentFingerprint = computeFingerprint(0, false, m_currentVehicleType,
                                                  m_currentVehicleId, m_currentBoardVersion);
    }
    qCDebug(vehicleRegistryLog)
        << "Vehicle identity:"
        << "\n  sysid:" << m_currentVehicleId
        << "\n  type:" << vehicleTypeString(m_currentVehicleType)
        << "\n  firmware:" << m_currentFirmwareVersion
        << "\n  uid:" << m_currentUid << " uid2:" << m_currentUid2Key
        << "\n  board:" << m_currentBoardVersion
        << "\n  source:" << m_currentFingerprintSource
        << "\n  fingerprint:" << m_currentFingerprint;
}

void VehicleRegistry::_settleIdentity()
{
    if (!m_currentVehicle || m_identitySettled) return;
    m_settleTimer.stop();
    _computeIdentity();
    m_identitySettled = true;
    _registerOrUpdateCurrentVehicle();
    emit currentVehicleChanged(m_currentVehicleId);
    emit identitySettled();
}

void VehicleRegistry::_onSettleTimeout()
{
    if (!m_currentVehicle || m_identitySettled) return;
    qCDebug(vehicleRegistryLog) << "No AUTOPILOT_VERSION after" << kSettleTimeoutMs
                                << "ms: settling the identity with what is known";
    _extractVehicleInfo(m_currentVehicle);
    _settleIdentity();
}

void VehicleRegistry::_resetIdentity()
{
    m_settleTimer.stop();
    m_identitySettled = false;
    m_registeredNewThisConnection = false;
    m_currentUid = 0;
    m_currentUid2Key.clear();
    m_currentBoardVersion.clear();
    m_currentFingerprint.clear();
    m_currentFingerprintSource.clear();
    m_currentHardwareUid.clear();
}

QString VehicleRegistry::currentDeviceKey() const
{
    if (!m_identitySettled || m_currentFingerprint.isEmpty())
        return QString();
    if (_hasValidUid(m_currentUid))
        return QString::number(m_currentUid);
    if (!m_currentUid2Key.isEmpty())
        return m_currentUid2Key;
    return m_currentFingerprint;
}

QString VehicleRegistry::uid2Key(const uint8_t *uid2, int len)
{
    if (!uid2 || len <= 0) return QString();
    int end = len;
    while (end > 0 && uid2[end - 1] == 0)
        --end;
    if (end == 0) return QString();
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(uid2), end).toHex());
}

QString VehicleRegistry::computeUid2Fingerprint(const QString &uid2Key, int mavType)
{
    // No board id: the uid2 already names one processor, and the board id
    // arrives from a different field than the one the fallback uses.
    const QString data = QStringLiteral("uid2=%1|type=%2").arg(uid2Key).arg(mavType);
    return QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex());
}

/// Concatenate UID, autopilot type, and board version, then SHA-256 hash them into a hex string.
QString VehicleRegistry::generateFingerprint(quint64 uid, int autopilotType, const QString &boardVersion)
{
    QByteArray data;
    data.append(QString::number(uid).toUtf8());
    data.append(QByteArray::number(autopilotType));
    data.append(boardVersion.toUtf8());
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

/// Map MAV_AUTOPILOT enum value to a short display string.
QString VehicleRegistry::autopilotTypeString(int autopilotType)
{
    switch (autopilotType) {
    case MAV_AUTOPILOT_GENERIC:       return QStringLiteral("Generic");
    case MAV_AUTOPILOT_ARDUPILOTMEGA: return QStringLiteral("ArduPilot");
    case MAV_AUTOPILOT_INVALID:       return QStringLiteral("Invalid");
    case MAV_AUTOPILOT_PX4:           return QStringLiteral("PX4");
    default:                          return QStringLiteral("Unknown(%1)").arg(autopilotType);
    }
}

/// Map MAV_TYPE enum value to a short display string.
QString VehicleRegistry::vehicleTypeString(int vehicleType)
{
    switch (vehicleType) {
    case 0:  return QStringLiteral("Generic");
    case 1:  return QStringLiteral("FixedWing");
    case 2:  return QStringLiteral("MultiRotor");
    case 3:  return QStringLiteral("VTOL");
    case 4:  return QStringLiteral("Rover");
    case 5:  return QStringLiteral("Sub");
    default: return QStringLiteral("Unknown(%1)").arg(vehicleType);
    }
}

/// Delegates vehicle search to DatabaseManager.
QString VehicleRegistry::searchVehicles(const QString &query)
{
    return DatabaseManager::instance().searchVehicles(query);
}

/// Update a vehicle's friendly name. If the fingerprint matches the currently
/// connected vehicle, updates the in-memory name and emits knownVehicleChanged.
bool VehicleRegistry::updateVehicleName(const QString &fingerprint, const QString &name)
{
    bool ok = DatabaseManager::instance().updateVehicleName(fingerprint, name);
    if (ok && fingerprint == m_currentFingerprint) {
        m_vehicleName = name;
        emit knownVehicleChanged();
    }
    return ok;
}

/// Return all registered vehicles as a JSON array string (for QML display).
QString VehicleRegistry::getAllVehiclesJson()
{
    return DatabaseManager::instance().getAllVehiclesJson();
}

/// Export all vehicle records as a JSON string for backup or transfer.
QString VehicleRegistry::exportVehiclesJson()
{
    return DatabaseManager::instance().exportVehiclesJson();
}

/// Import vehicle records from a previously-exported JSON string.
bool VehicleRegistry::importVehiclesJson(const QString &json)
{
    return DatabaseManager::instance().importVehiclesJson(json);
}