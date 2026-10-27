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
#include <QPair>
#include <QTimer>
#include <QVector>

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
    /// The pilot chose to fly without a registered pack and gave up the
    /// battery flight-time estimate. Cleared by choosing a pack, by the disarm
    /// that ends a flight (bench test arms keep it) and by a vehicle change.
    Q_PROPERTY(bool batteryPackWaived READ batteryPackWaived NOTIFY currentBatteryChanged)
    Q_PROPERTY(QString batteryPackWaivedBy READ batteryPackWaivedBy NOTIFY currentBatteryChanged)
    Q_PROPERTY(int currentFlightSessionId READ currentFlightSessionId NOTIFY currentVehicleChanged)
    Q_PROPERTY(double currentPayloadWeightKg READ currentPayloadWeightKg WRITE setPayloadWeightKg NOTIFY payloadWeightChanged)
    /// The pilot entered the payload for this connection (0 kg = none).
    Q_PROPERTY(bool payloadEntered READ payloadEntered NOTIFY payloadWeightChanged)
    Q_PROPERTY(QString currentLocationName READ currentLocationName WRITE setLocationName NOTIFY locationNameChanged)
    Q_PROPERTY(double planLatitude READ planLatitude WRITE setPlanLatitude NOTIFY planLatitudeChanged)
    Q_PROPERTY(double planLongitude READ planLongitude WRITE setPlanLongitude NOTIFY planLongitudeChanged)
    Q_PROPERTY(QString vehicleType READ vehicleType NOTIFY vehicleTypeResolved)
    Q_PROPERTY(QString vehicleKind READ vehicleKindString NOTIFY vehicleTypeResolved)
    Q_PROPERTY(int motorCount READ motorCount NOTIFY vehicleTypeResolved)
    Q_PROPERTY(bool typeResolved READ typeResolved NOTIFY vehicleTypeResolved)
    /// The connected aircraft's power type (frame + props + cells), -1 when
    /// it is not linked or no aircraft is connected.
    Q_PROPERTY(int currentPowerTypeId READ currentPowerTypeId NOTIFY powerTypeChanged)
    Q_PROPERTY(QString currentPowerTypeName READ currentPowerTypeName NOTIFY powerTypeChanged)
    /// Per flight: the pilot confirmed the props fitted match the power type,
    /// or waived the type, or waived a mission check that came out short.
    /// Cleared by the disarm that ends a flight (bench arms keep them) and by
    /// a vehicle change; a different type also clears the confirmation.
    Q_PROPERTY(bool powerTypePropsConfirmed READ powerTypePropsConfirmed NOTIFY powerTypeChanged)
    Q_PROPERTY(QString powerTypeWaivedBy READ powerTypeWaivedBy NOTIFY powerTypeChanged)
    Q_PROPERTY(QString missionCheckWaivedBy READ missionCheckWaivedBy NOTIFY powerTypeChanged)
    /// Minutes the copter stays at the target this flight; -1 = not entered.
    Q_PROPERTY(double timeAtTargetMin READ timeAtTargetMin NOTIFY payloadWeightChanged)

    /// Empty-airframe weight in kg (persisted per vehicle, used by the
    /// payload/battery flight-time estimate).
    Q_PROPERTY(double uavWeightKg READ uavWeightKg NOTIFY uavWeightChanged)

    /// Battery pack configuration for the live flight-time estimate (entered
    /// once per vehicle profile, persisted in vehicles.*): number of series
    /// cells, parallel cells, and cell capacity in mAh.
    Q_PROPERTY(int batterySeriesCells READ batterySeriesCells WRITE setBatterySeriesCells NOTIFY batteryConfigChanged)
    Q_PROPERTY(int batteryParallelCells READ batteryParallelCells WRITE setBatteryParallelCells NOTIFY batteryConfigChanged)
    Q_PROPERTY(int batteryCellMah READ batteryCellMah WRITE setBatteryCellMah NOTIFY batteryConfigChanged)
    /// Reserve fraction never flown through (default 0.20). 0..0.9.
    Q_PROPERTY(double batteryReserve READ batteryReserve WRITE setBatteryReserve NOTIFY batteryConfigChanged)
    /// Battery chemistry: "LiPo" (3.7V nom / 4.2V max), "LiIon" (3.6V/4.1V),
    /// "LiHV" (3.8V/4.35V). Persisted per vehicle.
    Q_PROPERTY(QString batteryType READ batteryType WRITE setBatteryType NOTIFY batteryConfigChanged)
    /// Pack capacity in Ah = N_parallel × cell_mAh / 1000. 0 when the profile
    /// is unconfigured (flight time can then not be displayed).
    Q_PROPERTY(double capacityAh READ capacityAh NOTIFY batteryConfigChanged)

    /// Live battery delivery into the FlyView chip — recomputed every 2 s by an
    /// internal timer from TelemetryBridge telemetry while a vehicle is connected.
    /// liveSOC is the selected pack's state of charge (packSocPct: pack rated mAh
    /// minus the FC's mAh used; the FC's battery_remaining only as fallback),
    /// 0-100, -1 when unknown.
    /// liveTimeMins = usable Ah above the reserve ÷ smoothed current × 60; 0 when
    /// the pack is at or below the reserve (land now); -1 when the gate
    /// (armed && I_smooth >= 3 A) is not met or data is missing.
    Q_PROPERTY(int liveSOC READ liveSOC NOTIFY liveDataChanged)
    Q_PROPERTY(double liveTimeMins READ liveTimeMins NOTIFY liveDataChanged)

public:
    static VehicleProfileManager *instance();
    explicit VehicleProfileManager(QObject *parent = nullptr);
    ~VehicleProfileManager() override;

    /// Map a MAV_TYPE integer to its canonical VehicleKind.
    static VehicleKind kindFromMavType(int mavType);
    /// Map a vehicle-type string ("QUAD", "FIXED_WING", ...) to its VehicleKind.
    static VehicleKind kindFromTypeString(const QString &type);
    /// Uppercase kind string: "MULTIROTOR", "FIXED_WING", "VTOL_CONVENTIONAL", "UNKNOWN".
    static QString kindString(VehicleKind kind);

    /// Connect to a TelemetryBridge to receive connection/arm state and vehicle parameters.
    void setTelemetryBridge(TelemetryBridge *bridge);

    /// Register the ArmingGate used to open a bounded, logged override window
    /// before force-arming (the Vehicle-level interceptor vetoes arm sends
    /// unless the gate allows it or an override is active).
    void setArmingGate(QObject *gate);

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
    bool batteryPackWaived() const { return !m_packWaivedBy.isEmpty(); }
    QString batteryPackWaivedBy() const { return m_packWaivedBy; }
    int currentFlightSessionId() const { return m_flightSessionId; }
    /// Adopt / release the flight_sessions row opened by the arming-gate flow.
    /// PreflightPlugin owns the session lifecycle; this controller only writes
    /// live battery energy into the single active row.
    void setFlightSessionId(int id) { m_flightSessionId = id; }

    double currentPayloadWeightKg() const { return m_payloadWeightKg; }
    void setPayloadWeightKg(double kg);
    bool payloadEntered() const { return m_payloadEntered; }
    /// The pilot's payload entry: sets the weight (kg, >= 0) and marks it
    /// entered. Returns false (nothing changes) for a negative or non-finite value.
    Q_INVOKABLE bool enterPayloadWeightKg(double kg);
    /// Forget the payload entry (vehicle change): weight back to 0, not entered.
    void clearPayloadEntry();

    /// Sets the empty-airframe weight. Accepts "kg" (default) or "lbs" and
    /// converts to kg before persisting to vehicles.uav_weight_kg.
    Q_INVOKABLE void setUavWeight(double value, const QString &unit = QStringLiteral("kg"));
    double uavWeightKg() const { return m_uavWeightKg; }

    int batterySeriesCells() const { return m_batterySeriesCells; }
    void setBatterySeriesCells(int cells);
    int batteryParallelCells() const { return m_batteryParallelCells; }
    void setBatteryParallelCells(int cells);
    int batteryCellMah() const { return m_batteryCellMah; }
    void setBatteryCellMah(int mah);
    double batteryReserve() const { return m_batteryReserve; }
    void setBatteryReserve(double reserve);
    QString batteryType() const { return m_batteryType; }
    void setBatteryType(const QString &type);
    double capacityAh() const { return m_batteryParallelCells * m_batteryCellMah / 1000.0; }

    /// Ah that may still be used before the reserve. The reserve is a share of
    /// the WHOLE pack (land with e.g. 20 % of the pack left), so this is
    /// capacity × (SOC − reserve), 0 at or below the reserve. (It used to be
    /// remaining × (1 − reserve), which never reached 0 and overstated the time
    /// most near the end: 2.4 min instead of 0 at 20 % SOC on a 5 Ah pack at 20 A.)
    /// -1 when capacity or SOC is unknown.
    static double usableAhAboveReserve(double capacityAh, int socPct, double reserve)
    {
        if (capacityAh <= 0.0 || socPct < 0)
            return -1.0;
        return qMax(0.0, capacityAh * (socPct / 100.0 - qBound(0.0, reserve, 1.0)));
    }
    /// State of charge (%) of the SELECTED pack: 100 × (1 − mAh used ÷ pack
    /// rated mAh), from the flight controller's mAh-consumed counter. The FC's
    /// own battery % is relative to its BATT_CAPACITY parameter, which may not
    /// match the pack fitted (e.g. ~3400 set for a 5000 mAh pack),
    /// so it is only the fallback. -1 when neither is known.
    static int packSocPct(int fcSocPct, double mahConsumed, double packCapacityAh)
    {
        if (packCapacityAh > 0.0 && mahConsumed >= 0.0)
            return qBound(0, qRound(100.0 * (1.0 - mahConsumed / (packCapacityAh * 1000.0))), 100);
        return fcSocPct >= 0 ? qBound(0, fcSocPct, 100) : -1;
    }

    /// The same with this profile's pack and reserve (for QML).
    Q_INVOKABLE double usableAhAboveReserveFor(int socPct) const
    {
        return usableAhAboveReserve(capacityAh(), socPct, m_batteryReserve);
    }

    /// Live estimate delivery (see Q_PROPERTY docs above).
    int liveSOC() const { return m_liveSOC; }
    double liveTimeMins() const { return m_liveTimeMins; }

    /// Persist the current pack config for the connected vehicle (no-op when
    /// no vehicle is connected).
    void _persistBatteryConfig();

    /// Force arm vehicle, bypassing pre-arm checks (funnels to TelemetryBridge).
    Q_INVOKABLE void forceArm();
    /// Disarm vehicle via MAVLink (set force=true to bypass in-flight/pre-disarm checks, sends kForceDisarmParam2, 21196).
    Q_INVOKABLE void disarmVehicle(bool force = false);
    /// Set vehicle flight mode (e.g. "MANUAL", "AUTO", "STABILIZE", "GUIDED").
    Q_INVOKABLE void setVehicleFlightMode(const QString &mode);

    /// Battery capacity in Wh from BATT_CAPACITY (mAh) × cell voltage, using
    /// live pack voltage to infer cell count (6S fallback). 0 when unknown.
    Q_INVOKABLE double batteryWh() const;

    // ── Motor thrust/current datasheet (battery time estimator) ─────────

    /// Human-readable summary of the active table, e.g.
    /// "HobbyWing X9 24×8 @33.6V · 9 pts" or "built-in default table".
    Q_PROPERTY(QString thrustTableInfo READ thrustTableInfo NOTIFY thrustTableChanged)
    /// Non-empty when the last datasheet operation needed a fallback
    /// (nearest voltage match, propeller group guess, parse problems).
    Q_PROPERTY(QString thrustTableWarning READ thrustTableWarning NOTIFY thrustTableChanged)

    /// Load a HobbyWing-style Motor Propeller Pull Test JSON from a file path
    /// or an embedded JSON payload (detected by a leading '{').  Filters the
    /// points to one propeller group (largest sample count when several are
    /// present) and to the voltage nearest preferredVoltageV (pass the pack's
    /// fully-charged voltage, N_series × 4.2).  On success the table replaces
    /// any previous one and is persisted to vehicles.motor_thrust_table for
    /// the connected vehicle.
    Q_INVOKABLE bool loadMotorDatasheet(const QString &jsonPathOrPayload,
                                        double preferredVoltageV = 0.0);
    /// Replace the table programmatically (list of {thrust_g, current_a} maps)
    /// and persist it; this is what the UI uses when operators hand-enter
    /// values derived from a PDF.
    Q_INVOKABLE void setThrustTable(const QVariantList &points);
    /// Linear interpolation of current(A) at thrust(g); clamps to the first /
    /// last table entry outside the measured range (callers show an
    /// extrapolation warning via maxTableThrust()).
    Q_INVOKABLE double currentForThrust(double thrustGrams) const;
    Q_INVOKABLE double minTableThrust() const;
    Q_INVOKABLE double maxTableThrust() const;
    QString thrustTableInfo() const { return m_thrustTableInfo; }
    QString thrustTableWarning() const { return m_thrustTableWarning; }

    QString currentLocationName() const { return m_locationName; }
    void setLocationName(const QString &name);

    double planLatitude() const { return m_planLat; }
    void setPlanLatitude(double lat);
    double planLongitude() const { return m_planLon; }
    void setPlanLongitude(double lon);

    /// Record battery serial for cycle tracking and persist to the battery table.
    Q_INVOKABLE void setBatterySerial(const QString &serial, const QString &operatorLabel = {});
    /// Fly this flight without a registered pack. Needs the pilot's name; the
    /// waiver is written to compliance_logs. Returns false (nothing changes)
    /// when @p pilot is blank.
    Q_INVOKABLE bool waiveBatteryPack(const QString &pilot);
    /// Drop the waiver (after a flight, or when the vehicle changes).
    void clearBatteryPackWaiver();
    Q_INVOKABLE QString loadVehicleHistory(const QString &deviceUid);
    Q_INVOKABLE QStringList knownVehicles();

    Q_INVOKABLE bool deleteVehicle(const QString &deviceUid);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                           const QString &notes);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);

    // ── Power types (storage in DatabaseManager, maths in PowerTypeModel.h) ──
    int currentPowerTypeId() const { return m_powerTypeId; }
    QString currentPowerTypeName() const;
    Q_INVOKABLE QVariantList listPowerTypes();
    /// Creates (@p id ≤ 0) or updates a type; returns its id or -1.
    Q_INVOKABLE int savePowerType(int id, const QString &frame, double propDiameterIn,
                                  double propPitchIn, int cells, const QString &motorNote);
    Q_INVOKABLE bool deletePowerType(int id);
    /// Links the connected aircraft to a type (≤ 0 unlinks). False with no aircraft.
    Q_INVOKABLE bool linkCurrentVehiclePowerType(int powerTypeId);
    /// The type's learned model: {status, source, hover0, hoverK, cruise0,
    /// cruiseK, advice, validated, chargerFactor, usedFlights, pendingFlights}.
    /// status: "none", "tooAlike", "payloadNotModelled", "payloadProportional",
    /// "full"; source: "charger" (fitted from charger numbers), "synthetic"
    /// (seeded from the FC's current until enough flights) or "none".
    Q_INVOKABLE QVariantMap powerTypeSummary(int powerTypeId);
    /// The type's flights (DatabaseManager::powerTypeFlights) plus, once the
    /// model is usable, predictedMah and outlier for each.
    Q_INVOKABLE QVariantList powerTypeFlights(int powerTypeId);
    Q_INVOKABLE bool setFlightChargerMah(int sessionId, double chargerMah);
    /// "used" adds the flight to its type's model, "dismissed" leaves it out.
    Q_INVOKABLE bool setFlightLearningState(int sessionId, const QString &state);

    bool powerTypePropsConfirmed() const { return m_propsConfirmed; }
    QString powerTypeWaivedBy() const { return m_powerTypeWaivedBy; }
    QString missionCheckWaivedBy() const { return m_missionWaivedBy; }
    double timeAtTargetMin() const { return m_timeAtTargetMin; }
    /// 0–600 min; false (nothing changes) outside that.
    Q_INVOKABLE bool enterTimeAtTargetMin(double minutes);
    /// The props fitted are the linked type's. False with no type linked.
    Q_INVOKABLE bool confirmPowerTypeProps();
    /// Fly this flight without a power type / with a mission check that came
    /// out short. Need the pilot's name; written to compliance_logs.
    Q_INVOKABLE bool waivePowerType(const QString &pilot);
    Q_INVOKABLE bool waiveMissionCheck(const QString &pilot);
    /// End of a (non-bench) flight or a vehicle change.
    void clearPowerTypeFlightState();
    /// The connected copter's FRAME_CLASS/FRAME_TYPE as a name ("Quad X"),
    /// empty when not reported; prefills a new power type's frame.
    Q_INVOKABLE QString currentFrameName() const;
    static QString frameName(int frameClass, int frameType);
    /// What the "Mission check" item and its dialog show: the trip to the
    /// saved target costed with the linked type's model. {applicable, linked,
    /// typeName, powerTypeWaivedBy, packWaived, modelUsable, validated,
    /// source, advice, payloadEntered, payloadKg, timeAtTargetEntered,
    /// minAtTarget, cruiseSpeedMs, speedAssumed, usableAh, hasTarget,
    /// positionKnown, targetKm, takeoffAh, outAh, targetAh, backAh, landAh,
    /// totalAh, totalMin, spareAh, maxTargetKm, fits, waivedBy}.
    Q_INVOKABLE QVariantMap missionCheck();

signals:
    void currentVehicleChanged();
    void uavWeightChanged();
    void batteryConfigChanged();
    void powerTypeChanged();
    void liveDataChanged();
    void thrustTableChanged();
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
    void _updateLiveEstimate();

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
    QObject *m_armingGate = nullptr;    ///< ArmingGate for force-arm override (set by PreflightPlugin).
    QString m_currentDeviceUid;
    QString m_currentVehicleHistoryJson;
    QString m_batterySerial;
    QString m_packWaivedBy;             ///< Pilot who waived the pack; empty when not waived.
    int m_flightSessionId = -1;
    int m_powerTypeId = -1;             ///< Connected aircraft's power type; -1 = none.
    bool m_propsConfirmed = false;
    QString m_powerTypeWaivedBy;
    QString m_missionWaivedBy;
    double m_timeAtTargetMin = -1.0;
    bool recordWaiver(const QString &event, const QString &pilot, const QString &reason, const QString &checkId);
    double m_payloadWeightKg = 0.0;
    bool m_payloadEntered = false;      ///< Set by enterPayloadWeightKg(), cleared on vehicle change.
    double m_uavWeightKg = 0.0;
    int m_batterySeriesCells = 6;       ///< N_series (default 6S).
    int m_batteryParallelCells = 1;     ///< N_parallel (default 1P).
    int m_batteryCellMah = 5000;        ///< Cell capacity (default 5000 mAh).
    double m_batteryReserve = 0.20;     ///< Reserve fraction never flown through.
    QString m_batteryType = QStringLiteral("LiPo");  ///< Chemistry (LiPo/LiIon/LiHV).

    // Live estimate state (updated every 2 s while a vehicle is connected).
    int m_liveSOC = -1;                 ///< battery_remaining (0-100, -1=unknown).
    double m_liveTimeMins = -1.0;       ///< -1 when the current gate is not met.
    double m_liveSmoothAmps = 0.0;      ///< EMA current (α=0.095 @ 0.5 Hz ≈ 10 s).
    QTimer m_liveTimer;                 ///< Drives the 2 s live-estimate refresh.
    QString m_locationName;
    double m_planLat = 0.0;
    double m_planLon = 0.0;
    QElapsedTimer m_sessionTimer;
    bool m_wasArmed = false;
    QElapsedTimer m_armedTimer;          // Tracks arm duration to filter out short test-arms.
    double m_armBatteryPct = -1.0;       // Battery % at arm time, used to compute energy used.

    // Thrust→current datasheet state (sorted ascending by thrust, g → A).
    QVector<QPair<double, double>> m_thrustTable;
    QString m_thrustTableInfo;
    QString m_thrustTableWarning;

    /// Install a table + meta and persist it for the connected vehicle.
    void _applyThrustTable(const QVector<QPair<double, double>> &table,
                           const QString &info, const QString &warning);
    /// Reset to the built-in default table (used on disconnect and when no
    /// per-vehicle datasheet has been persisted).
    void _applyDefaultThrustTable();
    /// Read vehicles.motor_thrust_table for deviceUid and install it.
    void _loadPersistedThrustTable(const QString &deviceUid);
};
