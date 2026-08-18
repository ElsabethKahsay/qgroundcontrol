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
/// On vehicle connect the registry explicitly requests AUTOPILOT_VERSION so the
/// hardware UID becomes available quickly.  Identity is handled in two tiers:
///   1. HARDWARE_UID   — SHA-256 of "uid|type=MAV_TYPE|board=boardVersion"
///   2. SYSID_TYPE_FALLBACK — SHA-256 of "sysid=N|type=MAV_TYPE|board=boardVersion"
/// used when the UID is still unknown or the hardware reports no UID.  A vehicle
/// is registered immediately with the fallback identity (so every board gets its
/// own record) and is upgraded in place once a real UID arrives.
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

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _onActiveVehicleChanged(Vehicle *vehicle);
    void _onVehicleUidChanged();
    void _onFirmwareVersionChanged();
    void _requestAutopilotCapabilities();
    void _onMavlinkMessage(const mavlink_message_t &message);

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
    void _upgradeToHardwareIdentity(Vehicle *vehicle, quint64 uid, const QString &boardVersion);

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
    QString m_currentHardwareUid;     ///< raw hex UID from AUTOPILOT_VERSION (e.g. "A1B2C3...")
    QString m_currentBoardVersion;

    QTimer m_autopilotRequestTimer;
};
