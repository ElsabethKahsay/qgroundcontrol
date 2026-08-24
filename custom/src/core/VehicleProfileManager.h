/**
 * @file VehicleProfileManager.h
 * @brief Manages per-vehicle profile data, flight sessions, and battery tracking.
 *
 * Resolves device UIDs, auto-starts/ends flight sessions on connect/disarm,
 * tracks battery cycles for health monitoring, and records GPS/payload metadata.
 * Listens to TelemetryBridge for connection and arm/disarm events.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QElapsedTimer>

class TelemetryBridge;

/// Canonical vehicle classification used to decide which preflight checks block,
/// which control surfaces exist, and what the hardware UI shows.
enum class VehicleKind : int {
    Multirotor,       ///< Quad, Hex, Octa, Tri
    FixedWing,        ///< Fixed wing, flying wing
    VtolConventional, ///< QuadPlane — has hover motors AND fixed-wing surfaces
    Unknown
};

/// Manages per-vehicle profile data: device UID resolution, flight session lifecycle,
/// battery tracking, and GPS/payload metadata.  Listens to TelemetryBridge for
/// connection and arm/disarm events to auto-start and end flight sessions in the database.
class VehicleProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentDeviceUid READ currentDeviceUid NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentVehicleHistoryJson READ currentVehicleHistoryJson NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentBatterySerial READ currentBatterySerial NOTIFY currentBatteryChanged)
    Q_PROPERTY(int currentFlightSessionId READ currentFlightSessionId NOTIFY currentVehicleChanged)
    Q_PROPERTY(double currentPayloadWeightKg READ currentPayloadWeightKg WRITE setPayloadWeightKg NOTIFY payloadWeightChanged)
    Q_PROPERTY(QString currentLocationName READ currentLocationName WRITE setLocationName NOTIFY locationNameChanged)
    Q_PROPERTY(double planLatitude READ planLatitude WRITE setPlanLatitude NOTIFY planLatitudeChanged)
    Q_PROPERTY(double planLongitude READ planLongitude WRITE setPlanLongitude NOTIFY planLongitudeChanged)
    Q_PROPERTY(QString vehicleType READ vehicleType NOTIFY vehicleTypeResolved)
    Q_PROPERTY(QString vehicleKind READ vehicleKindString NOTIFY vehicleTypeResolved)
    Q_PROPERTY(int motorCount READ motorCount NOTIFY vehicleTypeResolved)
    Q_PROPERTY(bool typeResolved READ typeResolved NOTIFY vehicleTypeResolved)

    /// Empty-airframe weight in kg (persisted per vehicle, used by the
    /// payload/battery flight-time estimate).
    Q_PROPERTY(double uavWeightKg READ uavWeightKg NOTIFY uavWeightChanged)

public:
    static VehicleProfileManager *instance();
    explicit VehicleProfileManager(QObject *parent = nullptr);

    /// Map a MAV_TYPE integer to its canonical VehicleKind.
    static VehicleKind kindFromMavType(int mavType);
    /// Map a vehicle-type string ("QUAD", "FIXED_WING", ...) to its VehicleKind.
    static VehicleKind kindFromTypeString(const QString &type);
    /// Uppercase kind string: "MULTIROTOR", "FIXED_WING", "VTOL_CONVENTIONAL", "UNKNOWN".
    static QString kindString(VehicleKind kind);

    /// Connect to a TelemetryBridge to receive connection/arm state and vehicle parameters.
    void setTelemetryBridge(TelemetryBridge *bridge);

    /// Resolve motor count from vehicle parameters (CA_AIRFRAME on PX4,
    /// FRAME_CLASS/FRAME_TYPE on ArduPilot). Falls back to defaultCount.
    static int resolveMotorCount(TelemetryBridge *telemetry, int defaultCount = 4);

    QString vehicleType() const { return m_vehicleType; }
    QString vehicleKindString() const { return kindString(m_kind); }
    VehicleKind kind() const { return m_kind; }
    int motorCount() const { return m_motorCount; }
    bool typeResolved() const { return m_typeResolved; }

    QString currentDeviceUid() const { return m_currentDeviceUid; }
    QString currentVehicleHistoryJson() const { return m_currentVehicleHistoryJson; }
    QString currentBatterySerial() const { return m_batterySerial; }
    int currentFlightSessionId() const { return m_flightSessionId; }

    double currentPayloadWeightKg() const { return m_payloadWeightKg; }
    void setPayloadWeightKg(double kg);

    /// Sets the empty-airframe weight. Accepts "kg" (default) or "lbs" and
    /// converts to kg before persisting to vehicles.uav_weight_kg.
    Q_INVOKABLE void setUavWeight(double value, const QString &unit = QStringLiteral("kg"));
    double uavWeightKg() const { return m_uavWeightKg; }

    /// Battery capacity in Wh from BATT_CAPACITY (mAh) × cell voltage, using
    /// live pack voltage to infer cell count (6S fallback). 0 when unknown.
    Q_INVOKABLE double batteryWh() const;

    QString currentLocationName() const { return m_locationName; }
    void setLocationName(const QString &name);

    double planLatitude() const { return m_planLat; }
    void setPlanLatitude(double lat);
    double planLongitude() const { return m_planLon; }
    void setPlanLongitude(double lon);

    /// Record battery serial for cycle tracking and persist to the battery table.
    Q_INVOKABLE void setBatterySerial(const QString &serial, const QString &operatorLabel = {});
    Q_INVOKABLE QString loadVehicleHistory(const QString &deviceUid);
    Q_INVOKABLE QStringList knownVehicles();

    Q_INVOKABLE bool deleteVehicle(const QString &deviceUid);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                           const QString &notes);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);

signals:
    void currentVehicleChanged();
    void uavWeightChanged();
    void currentBatteryChanged();
    void payloadWeightChanged();
    void locationNameChanged();
    void planLatitudeChanged();
    void planLongitudeChanged();
    void vehicleReconnected(const QString &deviceUid, const QString &friendlyName);
    void vehicleTypeResolved();

private slots:
    void onConnectionChanged();
    void _onArmedChanged(bool armed);
    void resolveVehicleTypeAndMotorCount();

private:
    static VehicleProfileManager *s_instance;
    QString m_vehicleType = QStringLiteral("UNKNOWN");
    VehicleKind m_kind = VehicleKind::Unknown;
    int m_motorCount = 0;
    bool m_typeResolved = false;

    /// Resolve the vehicle's persistent device UID: tries hardware UID first,
    /// then falls back to composite key or system ID.
    QString resolveDeviceUid();
    QString autopilotTypeString();
    /// Determine airframe type from vehicle parameters, falling back to HEARTBEAT flags.
    QString airframeTypeString();


    TelemetryBridge *m_telemetry = nullptr;
    QString m_currentDeviceUid;
    QString m_currentVehicleHistoryJson;
    QString m_batterySerial;
    int m_flightSessionId = -1;
    double m_payloadWeightKg = 0.0;
    double m_uavWeightKg = 0.0;
    QString m_locationName;
    double m_planLat = 0.0;
    double m_planLon = 0.0;
    QElapsedTimer m_sessionTimer;
    bool m_wasArmed = false;
    QElapsedTimer m_armedTimer;          // Tracks arm duration to filter out short test-arms.
    double m_armBatteryPct = -1.0;       // Battery % at arm time, used to compute energy used.
};
