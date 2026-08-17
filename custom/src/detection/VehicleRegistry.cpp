#include "VehicleRegistry.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtCore/QCryptographicHash>
#include <QtCore/QApplicationStatic>
#include <QtCore/QLoggingCategory>

#include "Vehicle/Vehicle.h"
#include "MultiVehicleManager.h"
#include "utils/DatabaseManager.h"
#include "managers/FlightSession.h"

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

    // Pick up any vehicle already connected at construction time.
    Vehicle *activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (activeVehicle) {
        _extractVehicleInfo(activeVehicle);
        if (m_currentUid == 0) {
            // Defer registration until the hardware UID arrives (see _onVehicleAdded).
            m_pendingVehicle = activeVehicle;
            connect(activeVehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);
        } else {
            _registerOrUpdateCurrentVehicle();
        }
    }
}

/// When a vehicle connects, build its fingerprint and check the database:
/// - Known vehicle: load friendly name, update last-seen timestamp, emit knownVehicleConnected.
/// - Unknown vehicle: generate a default name, persist a new record, emit newVehicleRegistered.
///
/// The fingerprint depends on the hardware UID (vehicleUID()), which is only
/// populated after the AUTOPILOT_VERSION request completes asynchronously.
/// If the UID is not known yet, defer registration until vehicleUIDChanged fires,
/// otherwise every board would hash to the same fingerprint (UID == 0) and all
/// devices would collapse into a single database record.
void VehicleRegistry::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) return;
    qCDebug(vehicleRegistryLog) << "Vehicle added: sysid" << vehicle->id();

    _extractVehicleInfo(vehicle);

    if (m_currentUid == 0) {
        qCDebug(vehicleRegistryLog) << "UID not yet available — deferring registration until AUTOPILOT_VERSION arrives";
        if (m_pendingVehicle) {
            disconnect(m_pendingVehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);
        }
        m_pendingVehicle = vehicle;
        connect(vehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);
        return;
    }

    _registerOrUpdateCurrentVehicle();
}

void VehicleRegistry::_onVehicleUidChanged()
{
    if (!m_pendingVehicle) return;
    qCDebug(vehicleRegistryLog) << "Vehicle UID now known:" << m_pendingVehicle->vehicleUID();

    Vehicle *vehicle = m_pendingVehicle;
    m_pendingVehicle = nullptr;
    disconnect(vehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);

    _extractVehicleInfo(vehicle);
    _registerOrUpdateCurrentVehicle();
}

void VehicleRegistry::_registerOrUpdateCurrentVehicle()
{
    if (m_currentUid == 0 || m_currentFingerprint.isEmpty()) {
        qCWarning(vehicleRegistryLog) << "Cannot register vehicle without a hardware UID";
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

        DatabaseManager::instance().registerNewVehicle(
            m_currentFingerprint, m_currentVehicleId, m_currentCompid,
            autopilotTypeString(m_currentAutopilotType),
            vehicleTypeString(m_currentVehicleType),
            m_currentFirmwareVersion, m_currentUid, m_currentBoardVersion,
            m_vehicleName);

        qCDebug(vehicleRegistryLog) << "New vehicle registered:" << m_vehicleName;
        emit newVehicleRegistered(m_currentVehicleId);
    }

    emit knownVehicleChanged();
}

void VehicleRegistry::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) return;
    qCDebug(vehicleRegistryLog) << "Vehicle removed: sysid" << vehicle->id();

    if (m_pendingVehicle == vehicle) {
        disconnect(vehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);
        m_pendingVehicle = nullptr;
    }

    FlightSession *fs = FlightSession::instance();
    if (fs) fs->closeSession();

    // Record the last-seen time before clearing state.
    if (!m_currentFingerprint.isEmpty()) {
        DatabaseManager::instance().updateVehicleLastSeen(m_currentFingerprint);
    }

    m_currentVehicleId = 0;
    m_currentFingerprint.clear();
    m_isKnownVehicle = false;
    m_vehicleName.clear();
    emit currentVehicleChanged(0);
    emit knownVehicleChanged();
}

void VehicleRegistry::_onActiveVehicleChanged(Vehicle *vehicle)
{
    if (vehicle) {
        _extractVehicleInfo(vehicle);
    } else {
        if (m_pendingVehicle) {
            disconnect(m_pendingVehicle, &Vehicle::vehicleUIDChanged, this, &VehicleRegistry::_onVehicleUidChanged);
            m_pendingVehicle = nullptr;
        }
        m_currentVehicleId = 0;
        m_currentFingerprint.clear();
        m_isKnownVehicle = false;
        m_vehicleName.clear();
        emit currentVehicleChanged(0);
        emit knownVehicleChanged();
    }
}

/// Pull hardware identifiers from the QGC Vehicle object and build the SHA-256 fingerprint.
void VehicleRegistry::_extractVehicleInfo(Vehicle *vehicle)
{
    if (!vehicle) return;

    m_currentVehicleId = vehicle->id();
    m_currentCompid = vehicle->defaultComponentId();
    m_currentAutopilotType = static_cast<int>(vehicle->firmwareType());
    m_currentVehicleType = static_cast<int>(vehicle->vehicleType());
    m_currentUid = vehicle->vehicleUID();
    m_currentBoardVersion = QString::number(vehicle->firmwareBoardProductId());

    int majorVer = vehicle->firmwareMajorVersion();
    int minorVer = vehicle->firmwareMinorVersion();
    int patchVer = vehicle->firmwarePatchVersion();
    m_currentFirmwareVersion = QStringLiteral("%1.%2.%3")
                                   .arg(majorVer)
                                   .arg(minorVer)
                                   .arg(patchVer);

    // Fingerprint = hash(UID + autopilot type + board version) — unique per physical board.
    m_currentFingerprint = generateFingerprint(m_currentUid, m_currentAutopilotType, m_currentBoardVersion);

    qCDebug(vehicleRegistryLog)
        << "Vehicle fingerprint:"
        << "\n  sysid:" << m_currentVehicleId
        << "\n  compid:" << m_currentCompid
        << "\n  autopilot:" << autopilotTypeString(m_currentAutopilotType)
        << "\n  type:" << vehicleTypeString(m_currentVehicleType)
        << "\n  firmware:" << m_currentFirmwareVersion
        << "\n  uid:" << m_currentUid
        << "\n  board:" << m_currentBoardVersion
        << "\n  fingerprint:" << m_currentFingerprint;
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
    case 0:  return QStringLiteral("Generic");
    case 1:  return QStringLiteral("PX4");
    case 2:  return QStringLiteral("ArduPilot");
    default: return QStringLiteral("Unknown(%1)").arg(autopilotType);
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
