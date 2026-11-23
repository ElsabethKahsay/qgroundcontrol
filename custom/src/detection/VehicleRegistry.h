#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>

#include "QGCMAVLink.h"

class Vehicle;
class HardwareTestController;

/// Singleton that identifies UAVs by a hardware fingerprint and tracks whether
/// the current vehicle is known/registered in the database.
/// Exposes QML-friendly properties for the preflight UI to display vehicle identity state.
///
/// On vehicle connect the registry explicitly requests AUTOPILOT_VERSION, which
/// carries the hardware ids and the board id. Identity is handled in two tiers:
///   1. HARDWARE_UID — AUTOPILOT_VERSION.uid when non-zero (key: its decimal),
///      else uid2 when non-zero (key: its hex, uid2Key()). ArduPilot ChibiOS
///      boards report uid = 0 and their STM32 id in uid2.
///   2. SYSID_TYPE_FALLBACK — SHA-256 of "sysid=N|type=MAV_TYPE|board=productId"
///      when the copter reports neither.
/// The identity is settled once per connection, when AUTOPILOT_VERSION arrives
/// (or after kSettleTimeoutMs for a vehicle that never sends it), and only then
/// is the vehicle looked up or registered: the heartbeat alone has no board id,
/// and registering on it made a second row once the board id arrived.
class VehicleRegistry : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("QML.Element", "singleton")
    Q_PROPERTY(int currentVehicleId READ currentVehicleId NOTIFY currentVehicleChanged)
    Q_PROPERTY(bool isKnownVehicle READ isKnownVehicle NOTIFY knownVehicleChanged)
    Q_PROPERTY(QString vehicleName READ vehicleName NOTIFY knownVehicleChanged)
    Q_PROPERTY(QString currentVehicleName READ currentVehicleName NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentFingerprint READ currentFingerprint NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentFingerprintSource READ currentFingerprintSource NOTIFY currentVehicleChanged)

public:
    static VehicleRegistry *instance();

    explicit VehicleRegistry(QObject *parent = nullptr);

    int currentVehicleId() const { return m_currentVehicleId; }
    bool isKnownVehicle() const { return m_isKnownVehicle; }
    QString vehicleName() const { return m_vehicleName; }
    QString currentVehicleName() const { return m_vehicleName; }
    QString currentFingerprint() const { return m_currentFingerprint; }
    QString currentFingerprintSource() const { return m_currentFingerprintSource; }
    /// The key the connected vehicle's records use (vehicles.device_uid): the
    /// decimal uid, the uid2 hex, or the fallback fingerprint. Empty until the
    /// identity is settled.
    QString currentDeviceKey() const;
    bool isIdentitySettled() const { return m_identitySettled; }

    /// AUTOPILOT_VERSION.uid2 as a key: hex of the bytes without the trailing
    /// zero padding; empty when all zero.
    static QString uid2Key(const uint8_t *uid2, int len);
    /// The fingerprint of a vehicle identified by its uid2.
    static QString computeUid2Fingerprint(const QString &uid2Key, int mavType);

    static constexpr int kSettleTimeoutMs = 4000;

    /// Search the vehicle database by name, UID, or fingerprint substring.
    Q_INVOKABLE QString searchVehicles(const QString &query);

    /// Rename a vehicle in the database; updates the display name if it matches the current vehicle.
    Q_INVOKABLE bool updateVehicleName(const QString &fingerprint, const QString &name);

    /// Return all registered vehicles as a JSON array string.
    Q_INVOKABLE QString getAllVehiclesJson();

    /// Export all vehicle records as a JSON string (for backup / import).
    Q_INVOKABLE QString exportVehiclesJson();

    /// Import vehicle records from a JSON string previously exported by exportVehiclesJson.
    Q_INVOKABLE bool importVehiclesJson(const QString &json);

    /// Build a fingerprint string from hardware UID, autopilot type, and board product ID.
    static QString generateFingerprint(quint64 uid, int autopilotType, const QString &boardVersion);

    /// Compute the identity fingerprint for a connection.  Uses the hardware
    /// UID when valid, otherwise falls back to sysid + vehicle type + board.
    /// @param hasUid   whether a meaningful hardware UID is available
    static QString computeFingerprint(quint64 uid, bool hasUid, int mavType, int sysid, const QString &boardVersion);

    /// Convert a MAV_AUTOPILOT enum value to a human-readable string ("PX4", "ArduPilot", etc.).
    static QString autopilotTypeString(int autopilotType);

    /// Convert a MAV_TYPE enum value to a human-readable string ("MultiRotor", "FixedWing", etc.).
    static QString vehicleTypeString(int vehicleType);

signals:
    void currentVehicleChanged(int vehicleId);
    void knownVehicleChanged();
    void newVehicleRegistered(int vehicleId);
    void knownVehicleConnected(int vehicleId);
    /// The connected copter's records moved from @p oldKey (its sysid-fallback
    /// key) to @p newKey (its decimal hardware UID). Emitted after the
    /// database re-key has committed, so a listener reloading from newKey finds
    /// the copter's setup there.
    void vehicleReKeyed(const QString &oldKey, const QString &newKey);
    /// The connected vehicle's identity is settled or changed (currentDeviceKey).
    void identitySettled();

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _onActiveVehicleChanged(Vehicle *vehicle);
    void _onVehicleUidChanged();
    void _onFirmwareVersionChanged();
    void _requestAutopilotCapabilities();
    void _onMavlinkMessage(const mavlink_message_t &message);
    void _onSettleTimeout();

private:
    /// Read hardware identifiers from a Vehicle and populate m_currentFingerprint.
    void _extractVehicleInfo(Vehicle *vehicle);
    /// Look up or register the current vehicle once hardware identity is known.
    void _registerOrUpdateCurrentVehicle();
    /// Refresh live attribute columns (type, firmware, frame, motors, ...) for the
    /// current vehicle so the Vehicles page always shows up-to-date details.
    void _updateCurrentVehicleAttributes(Vehicle *vehicle);
    /// (Re)apply the fingerprint + attributes when an AUTOPILOT_VERSION message
    /// supplies a better (hardware UID) identity than the current one.
    void _upgradeToHardwareIdentity(Vehicle *vehicle);
    /// Fix the connected vehicle's identity from what is known now (uid, uid2,
    /// board), then look it up or register it.
    void _settleIdentity();
    /// Fingerprint, source and hardware id for the current uid / uid2 / board.
    void _computeIdentity();
    void _resetIdentity();

    /// A valid uid must be non-zero (the UID may also arrive as an all-zero
    /// string when the hardware never provides one).
    static bool _hasValidUid(quint64 uid);

    Vehicle *m_pendingVehicle = nullptr;
    Vehicle *m_currentVehicle = nullptr;

    int m_currentVehicleId = 0;
    bool m_isKnownVehicle = false;
    QString m_vehicleName;
    QString m_currentFingerprint;
    QString m_currentFingerprintSource;
    int m_currentCompid = 0;
    int m_currentAutopilotType = 0;
    int m_currentVehicleType = 0;
    QString m_currentVehicleTypeName;
    int m_currentMotorCount = 0;
    int m_currentFrameClass = -1;
    QString m_currentFirmwareVersion;
    quint64 m_currentUid = 0;
    QString m_currentUid2Key;         ///< uid2Key() of AUTOPILOT_VERSION.uid2, "" when none
    QString m_currentHardwareUid;     ///< hex UID (uid) or the uid2 key; "" for the fallback
    QString m_currentBoardVersion;
    bool m_identitySettled = false;
    /// The row under the current (fallback) key was created by this connection:
    /// only such a row is renamed when a hardware id arrives later. A fallback
    /// row that existed before may belong to another copter with the same
    /// sysid, frame and board model, or to the simulator.
    bool m_registeredNewThisConnection = false;

    QTimer m_autopilotRequestTimer;
    QTimer m_settleTimer;
};
