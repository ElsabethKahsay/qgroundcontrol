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
#include <QHash>
#include <QPair>
#include <QSet>
#include <QTimer>

#include "MotorTable.h"
#include "PayloadRelease.h"
#include "WindFromLean.h"
#include "TerrainClearance.h"
#include <QVector>
#include <limits>

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
    /// Motor table the connected copter's estimate uses (MotorTable.h id); empty = none.
    Q_PROPERTY(QString motorTableId READ motorTableId NOTIFY motorTableChanged)
    /// Reason the last charger entry was refused (e.g. implausible for the
    /// pack), empty when it was accepted. The Vehicles page shows it.
    Q_PROPERTY(QString motorTableChargerError READ motorTableChargerError NOTIFY motorTableChanged)
    /// Per copter: the pilot who chose to fly this copter without a motor
    /// table; empty when not waived. Kept (QSettings, by device UID) across
    /// flights and reconnections until a motor table is linked to the copter.
    Q_PROPERTY(QString motorTableWaivedBy READ motorTableWaivedBy NOTIFY motorTableChanged)
    /// Minutes the copter stays at the target this flight; -1 = not entered.
    Q_PROPERTY(double timeAtTargetMin READ timeAtTargetMin NOTIFY payloadWeightChanged)
    /// Take-off and landing minutes costed as hover by the time left and the
    /// trip to a target when no mission is loaded (fleet settings, default
    /// 1 min each, 0–10). The mission estimate times them from the copter's
    /// own parameters instead.
    Q_PROPERTY(double takeoffMin READ takeoffMin NOTIFY motorTableChanged)
    Q_PROPERTY(double landMin READ landMin NOTIFY motorTableChanged)

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
    /// Battery chemistry: "LiPo" (3.7V nom / 4.2V max), "LiIon" (3.6V/4.2V),
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
    /// The battery % the Fly view shows. When the copter is linked to its
    /// motor table, is armed, its current sensor is known not to measure and
    /// the GCS's own count covers the flight, that count (powerLive.socNowPct);
    /// liveSOC otherwise. Every screen showing the battery % uses this one.
    Q_PROPERTY(int displaySocPct READ displaySocPct NOTIFY liveDataChanged)
    /// True when displaySocPct comes from the GCS rather than the flight
    /// controller (its count in flight, the pack's resting voltage on the
    /// ground, both because the current sensor reads 0 A); the tooltips use
    /// it to say where the % comes from.
    Q_PROPERTY(bool displaySocFromCount READ displaySocFromCount NOTIFY liveDataChanged)
    /// The battery estimate (copters linked to a motor table;
    /// PowerTypeModel::liveEstimate), refreshed with the live
    /// estimate: time and distance left, and whether the rest of the trip
    /// (to the target with the payload, the time there, home empty, land)
    /// still fits above the reserve. Values are -1 when unknown.
    Q_PROPERTY(bool powerLiveActive READ powerLiveActive NOTIFY liveDataChanged)
    Q_PROPERTY(QVariantMap powerLive READ powerLive NOTIFY liveDataChanged)
    /// The mission estimate in flight (missionEstimate() from where the copter
    /// is now), worked out again every 2 s while armed and airborne; empty
    /// otherwise. Extra keys: live, mode ("auto", "rtl", "land", "paused"),
    /// nextSeq, timeToNextS, returnNow, returnNowWhy, turnBackSeq, and passed
    /// (the items reached this flight: {seq, timeText, socPct, predictedTimeText,
    /// predictedSocPct} with the take-off prediction beside the real figures).
    /// Each waypoint also carries lat and lon (for the map labels).
    Q_PROPERTY(QVariantMap missionLive READ missionLive NOTIFY liveDataChanged)

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
    /// The pilot tapped "Payload dropped": the next payload is released (when
    /// no release output is set, or as a backup). False when none is aboard.
    Q_INVOKABLE bool payloadDroppedTap();
    /// A servo output's range on the copter, for the settings page:
    /// {known, minUs, maxUs (SERVOn_MIN / SERVOn_MAX), thresholdUs (the
    /// middle: "released" is past it), text}.
    Q_INVOKABLE QVariantMap servoOutputInfo(int output) const;
    /// The payload aboard now (kg): the entered payload less what has been
    /// released this flight; 0 after the target is reached.
    double livePayloadAboardKg() const;
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

    /// A charger number is plausible when the pack is unknown (the database's
    /// 1–200000 mAh limit still applies then) or at most 120 % of a known pack.
    static bool chargerMahPlausible(double chargerMah, double capacityMah)
    {
        return !(capacityMah > 0.0 && chargerMah > 1.2 * capacityMah);
    }

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

    /// True when the battery % must come from the GCS's own count: the
    /// estimate is running on a copter that is armed, whose current was found
    /// not to be measured, with a count whole from the arm.
    /// A GCS that joined a flight already armed, on a copter whose current
    /// sensor does not measure, has no idea of the charge used: the FC's %
    /// reads 100 all flight and must not stand in for the GCS's count. Advice
    /// only — nothing here holds the arm or changes what reaches the vehicle.
    static bool chargeUnknown(bool armed, bool sensorDead, bool countKnown)
    {
        return armed && sensorDead && !countKnown;
    }
    /// Why the charge is unknown, for the battery chip's tooltip.
    static QString chargeUnknownWhy();
    /// The in-flight mission estimate while the charge is unknown: it must not
    /// use the controller's % either (100 with a dead sensor), so nothing is
    /// costed and no RETURN NOW can be raised from a charge we cannot know.
    static QVariantMap missionLiveChargeUnknown();
    static bool socFromGcsCount(bool powerActive, bool armed, bool sensorDead, bool countKnown,
                                double socNowPct)
    {
        return powerActive && armed && sensorDead && countKnown && socNowPct >= 0.0;
    }
    /// On the ground, a copter whose current sensor does not measure still
    /// reports 100 % whatever the pack holds. With the estimate running, the
    /// pack's resting-voltage charge (packRestingSocPct) is shown instead.
    static bool restingSocShown(bool powerActive, bool armed, bool sensorDead, double restingSocPct)
    {
        return powerActive && !armed && sensorDead && restingSocPct >= 0.0;
    }
    /// The % the Fly view shows:
    ///   -1 when the charge is unknown (chargeUnknown(): joined armed with a
    ///      dead sensor), so a plain % display says "?" rather than 100;
    ///   in flight, the GCS's count when socFromGcsCount();
    ///   on the ground, the resting-voltage charge when restingSocShown();
    ///   the flight controller's liveSOC otherwise (-1 when it has none).
    static int displaySocPctFor(bool powerActive, bool armed, bool sensorDead, bool countKnown,
                                double socNowPct, int liveSoc, double restingSocPct = -1.0)
    {
        if (chargeUnknown(armed, sensorDead, countKnown))
            return -1;
        if (socFromGcsCount(powerActive, armed, sensorDead, countKnown, socNowPct))
            return qRound(socNowPct);
        if (restingSocShown(powerActive, armed, sensorDead, restingSocPct))
            return qRound(restingSocPct);
        return liveSoc;
    }

    /// The same with this profile's pack and reserve (for QML).
    Q_INVOKABLE double usableAhAboveReserveFor(int socPct) const
    {
        return usableAhAboveReserve(capacityAh(), socPct, m_batteryReserve);
    }

    /// Live estimate delivery (see Q_PROPERTY docs above).
    int liveSOC() const { return m_liveSOC; }
    double liveTimeMins() const { return m_liveTimeMins; }
    bool powerLiveActive() const { return m_powerLive.value(QStringLiteral("active")).toBool(); }
    bool displaySocFromCount() const;
    int displaySocPct() const;
    /// The pack's charge from its voltage read as a resting one
    /// (packRestingSocPct); -1 without a voltage reading or a pack set up.
    double displayRestingSocPct() const;
    /// {active, armed, airborne, hover, usableAh, currentA, timeLeftMin,
    /// distLeftKm, hoverMin, cruiseMin, cruiseKm, tripKnown, tripNeedAh,
    /// tripShortAh, returnNow, payloadAboard, source, typeName}.
    QVariantMap powerLive() const { return m_powerLive; }
    /// Each motor's throttle from its output signal, for the motor-table
    /// estimate: {throttles (0–1, motor order), channels, pwmMin, pwmMax,
    /// motorsExpected, ageMs, fresh}. fresh is false when the outputs are older
    /// than MotorTable::kMotorOutputStaleMs or a motor's output is not known.
    Q_INVOKABLE QVariantMap motorThrottleStatus() const;
    /// The motor table's view of the connected copter before take-off:
    /// {linked, tableName, motors, cells, cellsSource, planVolts, hoverSource
    /// ("last flight", "MOT_THST_HOVER" or empty), hoverThrustG (per motor),
    /// weightKg, hoverA (at planVolts, without payload), hoverAPerKg,
    /// hoverThrottle, issues [{level, text}], off (an issue stops the estimate)}.
    Q_INVOKABLE QVariantMap motorTableSetup() const;
    /// The air where the copter is, for the motor table: {ratio (density ÷
    /// sea-level density), pressureHpa, tempC, humidityPct, source}. Pressure
    /// from the flight controller's barometer, else the standard atmosphere at
    /// the GPS altitude; temperature and humidity from the weather service,
    /// else the standard atmosphere's temperature. Sea level when nothing is
    /// known.
    Q_INVOKABLE QVariantMap airDensity() const;
    /// The connected copter's flights on its motor table, newest first, each
    /// with {…as DatabaseManager::motorTableFlights, ratio (charger ÷ count),
    /// usable}. Empty when it is not on a table.
    Q_INVOKABLE QVariantList motorTableFlights() const;
    /// Enters the charger's mAh after a motor-table flight (1–200000, and at
    /// most 120 % of the pack when its capacity is known). On refusal the
    /// reason is in motorTableChargerError.
    Q_INVOKABLE bool setMotorTableFlightCharger(int flightId, double chargerMah);
    /// Leaves a motor-table flight out of the comparison (wrong pack charged…).
    Q_INVOKABLE bool dismissMotorTableFlight(int flightId);
    /// {usable, factor, proven, needed (flights still needed to prove)}.
    Q_INVOKABLE QVariantMap motorTableCalibration() const;

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
    QString motorTableChargerError() const { return m_chargerEntryError; }

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
    /// The connected copter's records moved from @p oldKey (its sysid-fallback
    /// key) to @p newKey (its hardware UID): take the new key and reload its
    /// setup from it. Another copter's re-key is ignored.
    void onVehicleReKeyed(const QString &oldKey, const QString &newKey);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                           const QString &notes);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);

    // ── Motor table (MotorTable.h): the estimate from each motor's throttle ──
    QString motorTableId() const { return m_motorTableId; }
    /// Links the connected copter to a motor table (empty unlinks). False with
    /// no aircraft or an unknown table.
    Q_INVOKABLE bool linkCurrentVehicleMotorTable(const QString &tableId);
    /// The tables the GCS knows: [{id, name, volts, motors, cells}].
    Q_INVOKABLE QVariantList motorTables() const;

    QString motorTableWaivedBy() const { return m_motorTableWaivedBy; }
    /// The selected pack's charge (%) from its resting voltage: 100 when full
    /// (or the voltage is not known), else PowerTypeModel::restingSocPct.
    double packRestingSocPct(double packVolts) const;
    double timeAtTargetMin() const { return m_timeAtTargetMin; }
    /// 0–600 min; false (nothing changes) outside that.
    Q_INVOKABLE bool enterTimeAtTargetMin(double minutes);
    /// Fly the connected copter without a motor table until one is linked.
    /// Needs the pilot's name and a connected copter; written to
    /// compliance_logs.
    Q_INVOKABLE bool waiveMotorTable(const QString &pilot);
    /// Withdraws the connected copter's waiver (linking a table does this).
    Q_INVOKABLE void clearMotorTableWaiver();
    /// The out-and-back trip to the saved target costed with the linked
    /// motor table (the battery estimate's numbers; the old "Mission check"
    /// item that showed it is replaced by the mission estimate).
    /// {applicable, linked,
    /// typeName, packWaived, modelUsable, validated,
    /// source, payloadEntered, payloadKg, timeAtTargetEntered,
    /// minAtTarget, cruiseSpeedMs, speedAssumed, usableAh, hasTarget,
    /// positionKnown, targetKm, takeoffAh, outAh, targetAh, backAh, landAh,
    /// totalAh, totalMin, spareAh, maxTargetKm, fits}.
    Q_INVOKABLE QVariantMap missionCheck();

    /// The mission estimate (MissionWalk → MissionCost → MissionAssess) for
    /// the mission on the copter, now. Keys: applicable, ready, problem,
    /// result, resultName, colour, landLikelyPct, landPessimisticPct, totalS,
    /// totalText, confidence, confidenceWhy, reasons, waypoints (list of
    /// {seq, what, timeText, socLikelyPct, socPessimisticPct, state,
    /// stateName}), emptyKg, payloadKg, payloadCount, dropText, weatherText,
    /// assumed, weatherRed, overCurrent. Advice only.
    Q_INVOKABLE QVariantMap missionEstimate();
    /// How accurate the mission estimate has been on this copter's recorded
    /// flights (MissionRecord.h): {flights, meanAbsSocPts, meanAbsTimePct,
    /// atOrAbovePessimistic, text}.
    Q_INVOKABLE QVariantMap missionAccuracy() const;
    QVariantMap missionLive() const { return m_missionLive; }

    /// Where the in-flight estimate starts: the copter now.
    struct LiveStart {
        double lat = 0.0, lon = 0.0, altM = 0.0;   ///< altitude above home
        int nextSeq = 1;                           ///< the mission item it is flying to
        int currentCommand = -1;                   ///< MAVLink command of the item being flown (-1 unknown)
        bool moving = false;
        QString mode;                              ///< "auto", "rtl", "land", "paused"
        double socPct = -1.0;                      ///< charge left now
        double payloadKg = 0.0;                    ///< still aboard
        /// The wind measured from the lean (toward, m/s); NaN: use the forecast.
        double windN = std::numeric_limits<double>::quiet_NaN();
        double windE = std::numeric_limits<double>::quiet_NaN();
    };
    /// missionEstimate() from @p live (nullptr: before take-off, from home).
    QVariantMap missionEstimateFrom(const LiveStart *live);
    /// The autopilot's flight mode as the in-flight estimate reads it:
    /// "auto" (flying the mission), "rtl" (any RTL), "land", else "paused".
    static QString liveModeOf(const QString &flightMode);
    /// Already on the way home or down: RTL (any), LAND, or AUTO flying a
    /// return or land item (@p currentCommand, the MAVLink command of the
    /// mission item flown now; -1 when not known).
    static bool isReturning(const QString &flightMode, int currentCommand);
    /// What the take-off estimate (missionEstimate() at the arm) predicted
    /// for mission item @p seq: {time from the arm in s, pessimistic charge
    /// on arrival in %}; {-1, -1} when it has no such item.
    static QPair<double, double> takeoffPredictionFor(const QVariantMap &takeoffEstimate, int seq);
    /// What the trip to a target (no mission estimated) advises when the rest
    /// of it no longer fits above the reserve (@p tripShort): RETURN NOW
    /// while still going out, but not when already returning or landing; then
    /// a warning that the landing will use the reserve.
    struct TripAdvice {
        bool returnNow = false;
        bool landingOnReserve = false;
    };
    static TripAdvice tripAdvice(bool tripShort, bool returning);
    /// RETURN NOW with hysteresis, so that it does not flicker (and is not
    /// spoken again and again) while the margin hovers around the reserve.
    /// @p marginPct: how far the return lands above the reserve, in points of
    /// the pack's charge (below 0: it no longer fits); NaN when not known.
    /// On as soon as the margin is below 0; once on (@p wasOn), off only when
    /// the margin is back to kReturnNowClearPts or more; never while already
    /// returning or landing; an unknown margin keeps the last state.
    static constexpr double kReturnNowClearPts = 3.0;
    static bool returnNowHeld(bool wasOn, double marginPct, bool returning);

    /// The in-flight mission estimate while the GPS is unreliable
    /// (TelemetryBridge::gpsPoor): marked (gpsPoor), the reason first among
    /// the reasons, and RETURN NOW's reason says the distance home may be
    /// wrong. The numbers stay: the charge is still right, where the copter
    /// is may not be.
    static QVariantMap withGpsPoor(QVariantMap live, bool gpsPoor);
    static inline const QString kGpsPoorReason =
        QStringLiteral("GPS unreliable: the position, the distances, the times and RETURN NOW may be wrong");
    /// The hover thrust per motor (g) a cruise ratio is measured against: a
    /// hover that was measured, never one worked out from MOT_THST_HOVER (a
    /// ratio against a wrong hover would be saved and used by the next
    /// flight's estimate). This flight's own steady hover once it has
    /// MotorTable::kMinHoverSamples (@p flightSum over @p flightSamples),
    /// else the hover remembered from an earlier flight (@p setupThrustG when
    /// @p setupSource is "last flight"); -1 when neither: no ratio is kept.
    static double cruiseReferenceThrustG(double flightSum, int flightSamples, double setupThrustG,
                                         const QString &setupSource);

    /// Whether the flight controller's current is measured, from what it
    /// reads in flight rather than from its monitor type (a DroneCAN monitor,
    /// BATT_MONITOR 8, can report the voltage and 0 A). While airborne with the
    /// motor table at kFcCurrentMinTableA or more: below kFcCurrentDeadA and
    /// kFcCurrentDeadFraction of the table for kFcCurrentWindowS in a row, it
    /// is not measuring (the GCS counts the charge itself); at
    /// kFcCurrentAliveFraction of the table or more for as long, it is again.
    /// In between, or without that evidence, nothing changes and the run of
    /// seconds starts again.
    struct FcCurrentCheck {
        bool dead = false;
        double deadS = 0.0;
        double aliveS = 0.0;
    };
    static constexpr double kFcCurrentMinTableA = 10.0;
    static constexpr double kFcCurrentDeadA = 2.0;
    static constexpr double kFcCurrentDeadFraction = 0.10;
    static constexpr double kFcCurrentAliveFraction = 0.50;
    static constexpr double kFcCurrentWindowS = 10.0;
    static FcCurrentCheck fcCurrentCheck(FcCurrentCheck s, bool airborne, double tableA, double fcA, double dtS);
    /// The connected copter's flight controller current found not to be
    /// measured (remembered per copter).
    bool fcCurrentDead() const { return m_fcCurrent.dead; }

    /// The GCS's own charge count for the closing flight record, when the
    /// flight controller's current sensor does not measure. Returns the raw
    /// mAh used and the % left (both -1 when the count is unknown). Raw, not
    /// charger-calibrated: motor_table_flights compares the charger against
    /// this same count. Computed from the accumulator, not powerLive, which
    /// the disarm refresh reports as a full pack. Read at disarm, before the
    /// next arm resets the count.
    QPair<double, double> recordedGcsCharge() const;

    // ── Terrain along the mission (TerrainClearance.h) ──
    /// The mission's legs as last worked out before take-off (the mission
    /// estimate keeps them; in flight the legs not yet flown are checked).
    QVector<TerrainClearance::Leg> terrainLegs() const { return m_terrainLegs; }
    /// Keeps the terrain under a leg (by TerrainClearance::Leg::key). The
    /// terrain query's answer comes here; tests give theirs directly. A
    /// profile that is kept is not replaced by a later answer.
    void setTerrainProfile(const QString &key, const TerrainClearance::Profile &profile);
    /// Whether the terrain is asked for (QGroundControl's terrain query,
    /// online with a cache). Tests turn it off and give the terrain
    /// themselves, so that they do not depend on the network.
    void setTerrainQueriesEnabled(bool enabled) { m_terrainQueriesEnabled = enabled; }
    /// The copter's parameters, three ways:
    /// - copterParam: QGroundControl's full parameter list; NaN when absent;
    /// - watchedParam: the bridge's watch list; @p fallback when not read;
    /// - anyParam: the watch list, else the full list; NaN when neither has it.
    double copterParam(const QString &name) const;
    double watchedParam(const QString &name, double fallback) const;
    double anyParam(const QString &name) const;
    /// Raises missionAlert() for what changed from the last in-flight
    /// estimate to @p next (the live update calls it every 2 s before keeping
    /// @p next): the result worse, RETURN NOW on, old weather once a flight.
    void checkMissionAlerts(const QVariantMap &next);
    /// The copter's weight without payload (kg) for the mission estimate,
    /// typed in by the operator and kept per copter; -1 when not entered.
    Q_INVOKABLE double emptyWeightKg() const;
    Q_INVOKABLE bool setEmptyWeightKg(double kg);

    double takeoffMin() const;
    double landMin() const;
    Q_INVOKABLE bool setTakeoffMin(double minutes);
    Q_INVOKABLE bool setLandMin(double minutes);

signals:
    void currentVehicleChanged();
    void uavWeightChanged();
    void batteryConfigChanged();
    /// The motor-table link, its waiver, its flights' charger numbers, or the
    /// take-off and landing minutes changed.
    void motorTableChanged();
    void liveDataChanged();
    void thrustTableChanged();
    void currentBatteryChanged();
    void payloadWeightChanged();
    void locationNameChanged();
    void planLatitudeChanged();
    void planLongitudeChanged();
    void vehicleReconnected(const QString &deviceUid, const QString &friendlyName);
    void vehicleTypeResolved();
    /// A payload came off in flight: `index` 1 or 2, `how` "output" or "tap".
    void payloadReleased(int index, double kg, const QString &how);
    /// The release output was at "released" at arming, or moved there while
    /// on the ground (`what` says which); nothing was counted as a drop.
    void payloadSwitchWarning(const QString &what);
    /// The empty weight was entered or changed.
    void emptyWeightChanged();
    /// Terrain under one of the mission's legs arrived (or could not be had).
    void terrainChanged();
    /// Something the pilot should hear about in flight: the result got
    /// worse, RETURN NOW came on, the weather in use is old. @p speak: also
    /// say it (not only a message on screen).
    void missionAlert(const QString &text, bool speak);

private slots:
    void onConnectionChanged();
    /// Loads the connected copter's stored setup under m_currentDeviceUid
    /// (weight, pack, thrust and motor tables, the remembered sensor and pack
    /// readings). The dead-sensor flag is kept if this session found it.
    void _loadVehicleProfile(const QString &apType, const QString &afType);
    void _onArmedChanged(bool armed);
    void resolveVehicleTypeAndMotorCount();
    void _updateLiveEstimate();
    void _updatePowerLive(double fcMahUsed, int socPct);

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
    QString m_motorTableId;             ///< Connected copter's motor table; empty = none.
    double m_tableSmoothA = 0.0;        ///< Motor-table battery current, smoothed like m_liveSmoothAmps.
    double m_tableSmoothW = 0.0;        ///< The motors' power from the table, smoothed the same way.
    /// The motor-output readings since the last 2 s update: their motors'
    /// power summed, how many, and when the last one came. The charge is
    /// counted on each reading as it arrives.
    double m_readingWattsSum = 0.0;
    int m_readings = 0;
    qint64 m_lastReadingMs = -1;
    double m_lastSigma = 1.0;           ///< The air density the last update used.
    /// Counts one motor-output reading (TelemetryBridge::motorOutputsReceived).
    void _onMotorOutputs();
    /// The pack's internal resistance (Ω): remembered from its last flight
    /// (QSettings, per aircraft), and measured again in each flight.
    double m_packOhm = -1.0;
    /// This flight's level-cruise readings: cruise power ÷ hover power for
    /// the same copter and air, summed over all speeds (for the flight record)
    /// and per speed band (MotorTable::cruiseBand). A band with enough
    /// readings is remembered per aircraft at the disarm (QSettings
    /// MotorTable/cruiseBand/<uid>/<band>).
    double m_cruiseRatioSum = 0.0;
    int m_cruiseSamples = 0;
    QHash<int, QPair<double, int>> m_cruiseBands;
    /// This flight's record for the charger comparison (motor_table_flights),
    /// written at the disarm of a flight that got airborne.
    struct TableFlightLog {
        QString startedAt;
        bool startedFull = false;
        double startSoc = -1.0;
        bool airborne = false;
        double voltSum = 0.0;
        int voltN = 0;
        double minVolts = -1.0;
        double hoverWSum = 0.0;
        double hoverS = 0.0;
        double cruiseS = 0.0;
        double beyondS = 0.0;
        double overallThrottleS = 0.0;
        double densitySum = 0.0;
        int densityN = 0;
        qint64 lastMs = -1;
    } m_tableLog;
    bool m_armVoltsKnown = false;       ///< The resting voltage was read before the arm.
    /// Fine-tuning and proof from the copter's recorded flights on its table.
    MotorTable::Calibration motorTableCalibrationNow() const;
    double m_packOhmLive = -1.0;
    int m_packOhmSamples = 0;
    /// The pack the motor-table estimate works with: cells, chemistry, size,
    /// and the resistance (remembered, else a typical value for its cells).
    MotorTable::Pack motorTablePack(int cells) const;
    /// This flight's steady hover without payload (thrust per motor, g), summed
    /// per 2 s update; stored at the disarm for the next take-off.
    double m_hoverThrustSum = 0.0;
    int m_hoverSamples = 0;
    QString m_motorTableWaivedBy;
    double m_armStartSocPct = 100.0;    ///< Pack charge (%) just before this flight's arm.
    double m_fcMahAtArm = -1.0;         ///< The FC's mAh counter at this flight's arm (it counts from power-up).
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
    QVariantMap m_powerLive;            ///< Last power-type live estimate.
    bool m_liveWasArmed = false;
    bool m_livePayloadAboard = false;   ///< This flight's payload, until the target is reached.
    /// The release servo output (Preflight Settings); counts the payloads
    /// released this flight.
    PayloadRelease::Detector m_drop;
    QVariantMap m_missionLive;          ///< missionLive()
    void _updateMissionLive(int socPct, bool chargeUnknown = false);
    // This flight, from the arm: the take-off estimate, the items reached
    // ({seq, timeS, socPct}), the mission item flown last, and what has been
    // alerted already.
    qint64 m_flightStartMs = 0;
    QVariantMap m_takeoffEstimate;      ///< missionEstimate() at the arm, for the flight's record
    QVariantList m_dropEvents;          ///< payload releases this flight: {index, kg, timeS, how}
    void _noteDrop(int index, double kg, const QString &how);
    QVariantList m_passed;
    int m_lastMissionIndex = -1;
    int m_lastAlertResult = -1;         ///< the worst result said or seen this flight; -1 = none yet
    // Terrain under the mission's legs: the legs from the last estimate made
    // before take-off, their profiles by key, the queries out, and when one
    // failed (asked again after kTerrainRetryMs).
    QVector<TerrainClearance::Leg> m_terrainLegs;
    QHash<QString, TerrainClearance::Profile> m_terrainProfiles;
    QSet<QString> m_terrainPending;
    QHash<QString, qint64> m_terrainFailedMs;
    bool m_terrainQueriesEnabled = true;
    // The flight controller's current, judged from what it reads in flight
    // (fcCurrentCheck); remembered per copter in QSettings.
    FcCurrentCheck m_fcCurrent;
    qint64 m_fcCheckLastMs = 0;
    void setFcCurrentDead(bool dead);
    // A first answer is often "no data yet": QGroundControl's terrain query
    // answers from its tile cache at once and only starts downloading the
    // tiles then, which takes a few seconds. Asked again soon, so that the
    // check is done before the pilot arms (a cached tile is answered at once).
    static constexpr qint64 kTerrainRetryMs = 5000;
    QString m_terrainLogged;            ///< the last terrain summary logged before take-off
    void requestTerrain(QVector<TerrainClearance::Leg> legs);
    /// Keeps a profile unless one is kept already; true when it was kept.
    bool keepTerrainProfile(const QString &key, const TerrainClearance::Profile &profile);
    bool m_alertedOldWeather = false;
    bool m_alertedGpsPoor = false;         // said once per flight
    // The wind from the lean (WindFromLean.h): this flight's steady samples,
    // the copter's drag constant (saved per copter, learned on flights that
    // turned; 0 = not known) and the last ground velocity, for the speed-up
    // check.
    QVector<WindFromLean::Sample> m_windSamples;
    double m_windK = 0.0;
    bool m_windKLearnedThisFlight = false;
    /// The last wind from the lean (toward, m/s), NaN when none: the cruise
    /// power is kept by airspeed with it.
    double m_windEstN = std::numeric_limits<double>::quiet_NaN();
    double m_windEstE = std::numeric_limits<double>::quiet_NaN();
    double m_windLastN = std::numeric_limits<double>::quiet_NaN();
    double m_windLastE = std::numeric_limits<double>::quiet_NaN();
    qint64 m_windLastMs = 0;
    /// Adds a steady sample now and works out the wind: with the drag
    /// constant known, from the last minute; else learning it from the flight
    /// so far. False when no wind can be given yet.
    bool _windFromLean(double *windN, double *windE);
    PayloadRelease::Config dropConfig() const;
    /// The release threshold for servo output @p output: the middle of its
    /// SERVOn_MIN / SERVOn_MAX on the copter (PayloadRelease::threshold).
    double releaseThresholdUs(int output) const;

    void _onOutputsForDrop();
    void _recordDrop(const QString &eventType, const QString &detail);
    /// The GCS's own count of the charge used this flight (copters whose FC
    /// reports no current that follows throttle), from the arm.
    double m_gcsUsedAh = 0.0;
    bool m_gcsCounting = false;         ///< Counting since an arm seen on this connection.
    bool m_gcsCountKnown = false;       ///< The count covers the whole flight (false when joined armed).
    bool m_liveSeenDisarmed = false;    ///< The vehicle was seen disarmed on this connection.
    qint64 m_gcsLastMs = -1;
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
    QString m_chargerEntryError;

    /// Install a table + meta and persist it for the connected vehicle.
    void _applyThrustTable(const QVector<QPair<double, double>> &table,
                           const QString &info, const QString &warning);
    /// Reset to the built-in default table (used on disconnect and when no
    /// per-vehicle datasheet has been persisted).
    void _applyDefaultThrustTable();
    /// Read vehicles.motor_thrust_table for deviceUid and install it.
    void _loadPersistedThrustTable(const QString &deviceUid);
};
