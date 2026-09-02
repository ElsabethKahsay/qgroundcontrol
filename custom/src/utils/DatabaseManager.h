// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/DatabaseManager.h
// Description: SQLite wrapper for templates, compliance logs, vehicle/battery profiles.

#pragma once

// ============================================================================
// DatabaseManager — Singleton providing all SQLite persistence for the
// UAV Preflight Checklist plugin.  Owns a single .db file containing
// 15 tables that together cover:
//
//   Schema tracking     schema_version          Stores the current DB version
//                                                for incremental migrations.
//
//   Templates           checklist_templates     Saved checklist templates,
//                                                keyed by vehicle type + name.
//
//   Compliance          compliance_logs         Finished checklist runs with
//                                                full JSON snapshot + telemetry.
//
//   Hardware audit      hardware_test_events    Servo / actuator test steps
//                      motor_test_results       Motor spin-up test results
//
//   Maintenance         maintenance_components  Hours & cycle tracking for
//                                                replaceable parts (motors,
//                                                props, batteries, etc.).
//
//   Vehicles            vehicles                Master vehicle registry keyed
//                                                  by device UID (hardware UID
//                                                  or fingerprint).
//                      vehicle_config          Per-vehicle check overrides as
//                                                  a JSON blob.
//
//   Batteries           batteries               Master battery registry keyed
//                                                  by serial number.
//                      battery_cycles          Per-flight cycle & health data.
//
//   Flights             flight_sessions         Start/end time, payload,
//                                                  energy, and location per flight.
//
//   Check audit         check_results           Per-check pass/fail records
//                                                  tied to a flight session.
//                      check_config            Per-check key/value overrides,
//                                                  optionally scoped to a vehicle.
//
//   Airspace            no_fly_zones            Known restricted airspace
//                                                  (regulatory, obstacle, etc.).
//                      zone_compliance_log      Manual compliance check records
//                                                  tied to a flight (audit trail).
//
// Thread affinity: All public methods MUST be called from the main (GUI)
// thread.  DatabaseManager owns a QSqlDatabase connection which is not
// thread-safe.  If background I/O is ever needed, the connection must be
// moved to a dedicated thread with its own event loop, or queries must be
// serialized through a worker object.
//
// All public methods are Q_INVOKABLE so they can be called directly from QML.
// Every mutator returns bool (success/fail); every query returns either a
// JSON string, a QStringList, or a plain value.
// ============================================================================

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QVariantMap>

/// Lightweight representation of a row in the vehicle registry.
/// Used externally for serialisation; the DB methods return JSON strings
/// instead of this struct, so it mainly serves as documentation.
struct VehicleRecord {
    int sysid = 0;
    int compid = 0;
    QString fingerprint;
    QString fingerprintSource;    ///< "HARDWARE_UID" or "SYSID_TYPE_FALLBACK"
    QString autopilotType;        ///< "ArduPilot", "PX4", "Unknown"
    QString vehicleType;          ///< short airframe string ("FixedWing", "MultiRotor")
    QString vehicleTypeName;      ///< human-readable ("Fixed Wing", "Quadcopter", ...)
    QString firmwareVersion;      ///< e.g. "4.5.2"
    quint64 uid = 0;              ///< numeric UID (0 = not resolved)
    QString hardwareUid;          ///< raw hex UID string from AUTOPILOT_VERSION
    QString boardVersion;         ///< firmware_board_product_id as string
    QString displayName;
    QDateTime firstSeen;
    QDateTime lastSeen;
    int totalFlightCount = 0;
    double totalFlightHours = 0.0;
    int totalFlightTimeSec = 0;
    int frameClass = -1;          ///< FRAME_CLASS / CA_AIRFRAME value, -1 = unknown
    int motorCount = 0;
    QString notes;
};

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager &instance();

    Q_INVOKABLE bool initialize(const QString &dbPath = QString());

    // ── Template CRUD ─────────────────────────────────────────────────
    Q_INVOKABLE bool saveTemplate(const QString &templateId, const QString &vehicleType,
                                const QString &templateName, const QString &templateJson);
    Q_INVOKABLE QString loadTemplate(const QString &templateId);
    Q_INVOKABLE QStringList listTemplates(const QString &vehicleType);
    Q_INVOKABLE bool deleteTemplate(const QString &templateId);

    // ── Compliance log CRUD ───────────────────────────────────────────
    Q_INVOKABLE bool saveComplianceLog(const QString &logId, const QString &vehicleId,
                                       const QString &vehicleType, const QString &operatorId,
                                       const QString &logJson, const QString &telemetrySnapshot);
    Q_INVOKABLE QString loadComplianceLog(const QString &logId);
    Q_INVOKABLE QStringList listComplianceLogs(const QString &vehicleId, int limit = 50);
    Q_INVOKABLE bool deleteComplianceLog(const QString &logId);

    // ── Hardware test event logging ───────────────────────────────────
    Q_INVOKABLE bool logHardwareTestStep(
        int flightId, const QString &stepName, int servoInstance,
        int targetPwm, int feedbackPwm, int toleranceMin, int toleranceMax,
        bool operatorConfirmed, const QString &result,
        const QString &failureReason = "", const QString &operatorId = "", int durationMs = 0);
    Q_INVOKABLE bool logMotorTestResult(int vehicleSysId, int motorIndex, int throttlePct,
                                        int durationSec, int expectedPwm, int actualPwm,
                                        int pwmDelta, const QString &result);
    Q_INVOKABLE bool logSurfaceTestResult(int flightId, const QString &surfaceId, int channel,
                                          int minPwmActual, int maxPwmActual,
                                          int directionOk, const QString &result);
    Q_INVOKABLE QString getHardwareTestEvents(int flightId);

    // ── Component maintenance CRUD ────────────────────────────────────
    Q_INVOKABLE bool addComponent(const QString &name, const QString &type,
                                  double maxHours, int maxCycles);
    Q_INVOKABLE bool updateComponentHours(int id, double hours);
    Q_INVOKABLE bool incrementComponentCycle(int id);
    Q_INVOKABLE bool resetComponentMaintenance(int id, const QString &notes = QString());
    Q_INVOKABLE bool deleteComponent(int id);
    Q_INVOKABLE QString listComponentsJson();

    // ── Vehicle profile CRUD ──────────────────────────────────────────
    Q_INVOKABLE bool upsertVehicle(const QString &deviceUid, const QString &friendlyName,
                                   const QString &autopilotType, const QString &airframeType);
    Q_INVOKABLE bool upsertVehicleEx(const QString &deviceUid, const QString &friendlyName,
                                     const QString &autopilotType, const QString &airframeType,
                                     int frameClass = -1, int frameType = -1,
                                     int motorCount = 0, const QString &motorLayout = QString(),
                                     double gpsLat = 0.0, double gpsLon = 0.0);

    /// Empty-airframe weight (kg) persistence for the payload/battery estimate.
    Q_INVOKABLE bool updateVehicleUavWeight(const QString &deviceUid, double weightKg);
    Q_INVOKABLE double vehicleUavWeight(const QString &deviceUid) const;

    /// Thrust→current datasheet JSON persistence for the battery time estimator.
    /// Empty string means "use the built-in default table".
    Q_INVOKABLE bool updateVehicleMotorThrustTable(const QString &deviceUid, const QString &json);
    Q_INVOKABLE QString vehicleMotorThrustTable(const QString &deviceUid) const;
    Q_INVOKABLE bool updateVehicleBatteryConfig(const QString &deviceUid, int series,
                                                int parallel, int cellMah, double reserve,
                                                const QString &batteryType);
    Q_INVOKABLE bool vehicleBatteryConfig(const QString &deviceUid, int *series,
                                          int *parallel, int *cellMah, double *reserve,
                                          QString *batteryType) const;
    Q_INVOKABLE QString getVehicle(const QString &deviceUid);
    Q_INVOKABLE QStringList listVehicles();
    Q_INVOKABLE bool deleteVehicle(const QString &deviceUid);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateFlightHours(const QString &deviceUid, double hours);
    Q_INVOKABLE bool updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                           const QString &notes);
    Q_INVOKABLE bool updateVehicleGps(const QString &deviceUid, double lat, double lon);
    Q_INVOKABLE bool updatePreflightStatus(const QString &deviceUid, const QString &status);
    Q_INVOKABLE QString exportVehiclesJson();
    Q_INVOKABLE bool importVehiclesJson(const QString &json);
    Q_INVOKABLE QString searchVehicles(const QString &query);

    // ── Vehicle registry (fingerprint-based) ─────────────────────────
    // Vehicles can be identified either by a stable hardware UID or by a
    // connection-fingerprint string built from vehicle properties.  These
    // methods handle the fingerprint path — used when the autopilot
    // connection supplies a unique vehicle signature via HEARTBEAT.
    Q_INVOKABLE QString lookupVehicleByFingerprint(const QString &fingerprint);
    Q_INVOKABLE bool registerNewVehicle(const QString &fingerprint, int sysid, int compid,
                                        const QString &autopilotType, const QString &vehicleType,
                                        const QString &vehicleTypeName, const QString &firmwareVersion,
                                        quint64 uid, const QString &hardwareUid,
                                        const QString &boardVersion, const QString &displayName,
                                        const QString &fingerprintSource);
    Q_INVOKABLE bool updateVehicleLastSeen(const QString &fingerprint);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);
    Q_INVOKABLE bool updateVehicleName(const QString &fingerprint, const QString &name);
    Q_INVOKABLE bool updateVehicleFingerprint(const QString &oldFingerprint, const QString &newFingerprint,
                                              const QString &newSource);
    Q_INVOKABLE bool updateVehicleAttributes(const QString &fingerprint, const QString &autopilotType,
                                             const QString &vehicleType, const QString &vehicleTypeName,
                                             const QString &firmwareVersion, const QString &hardwareUid,
                                             int sysid, const QString &boardVersion,
                                             int frameClass, int motorCount);
    Q_INVOKABLE bool incrementVehicleFlightTime(const QString &fingerprint, int durationSeconds);
    Q_INVOKABLE QString exportVehiclesCsv(const QString &fromDate, const QString &toDate);
    Q_INVOKABLE QString getAllVehiclesJson();

    // ── Vehicle check config ──────────────────────────────────────────
    Q_INVOKABLE bool saveVehicleConfig(const QString &fingerprint, const QString &configJson);
    Q_INVOKABLE QString loadVehicleConfig(const QString &fingerprint);

    // ── Battery CRUD ──────────────────────────────────────────────────
    Q_INVOKABLE bool upsertBattery(const QString &serialNumber, const QString &operatorLabel);
    Q_INVOKABLE QString getBattery(const QString &serialNumber);
    Q_INVOKABLE QStringList listBatteries();

    // ── Battery cycle CRUD ────────────────────────────────────────────
    Q_INVOKABLE bool saveBatteryCycle(const QString &serialNumber, int flightSessionId,
                                      double capacityAtFullMah, double voltageSagV,
                                      double restingVoltageV, int cycleCount);
    Q_INVOKABLE QString getBatteryCycles(const QString &serialNumber, int limit = 20);
    Q_INVOKABLE int getBatteryCycleCount(const QString &serialNumber);
    Q_INVOKABLE QString getBatteryHealthTrend(const QString &serialNumber);
    Q_INVOKABLE bool incrementBatteryCycle(int flightSessionId, const QString &batterySerial = QString());

    // ── Flight session CRUD ───────────────────────────────────────────
    Q_INVOKABLE int startFlightSession(const QString &deviceUid, const QString &batterySerial,
                                        double payloadWeightKg = 0.0);
    Q_INVOKABLE bool updateFlightSessionPayload(int sessionId, double payloadWeightKg);
    Q_INVOKABLE bool updateFlightSessionLocation(int sessionId, const QString &locationName);
    Q_INVOKABLE bool updateFlightSessionPlanLocation(int sessionId, double lat, double lon);
    /// Persists the operator-confirmed target location for a flight session
    /// (lat/lon plus a source tag: "map center" / "manual").  Returns false
    /// when the session id is unknown or the values are out of range.
    Q_INVOKABLE bool saveTargetLocation(int sessionId, double lat, double lon,
                                        const QString &source);
    /// Returns {lat, lon, source} for the session, or an empty map when no
    /// target location was saved (or the session id is unknown).
    Q_INVOKABLE QVariantMap getTargetLocation(int sessionId);
    Q_INVOKABLE bool endFlightSession(int sessionId, double durationSeconds);
    Q_INVOKABLE bool updateFlightSessionEnergy(int sessionId, double energyConsumedWh, double distanceM);
    Q_INVOKABLE QString getFlightSessions(const QString &deviceUid, int limit = 10);
    Q_INVOKABLE QString getVehicleHistory(const QString &deviceUid);

    /// Calibrated power model derived from completed flight sessions.
    /// Aggregates energy consumption data to estimate Wh/km for range
    /// prediction.  Only considered calibrated when enough data points
    /// exist (controlled by minSessions parameter).
    struct CalibratedPowerModel {
        double whPerKm = 0.0;
        double hoverWhPerMin = 0.0;  // estimated from average hover duration
        int dataPointCount = 0;
        bool isCalibrated = false;
    };
    /// Query average energy consumption from the last N completed sessions for a vehicle.
    CalibratedPowerModel getCalibratedPowerModel(const QString &deviceUid, int minSessions = 5);

    // ── Check config (check-level overrides) ──────────────────────────
    Q_INVOKABLE QString getCheckConfig(const QString &checkId, const QString &key,
                                       int vehicleId = -1);
    Q_INVOKABLE bool setCheckConfig(const QString &checkId, const QString &key,
                                    const QString &value, int vehicleId = -1);

    // ── Check result audit ────────────────────────────────────────────
    Q_INVOKABLE bool saveCheckResult(const QString &deviceUid, int flightSessionId,
                                     const QString &checkId, const QString &status,
                                     const QString &message);
    Q_INVOKABLE QString getCheckResults(int flightSessionId);

    // ── Operator CRUD ──────────────────────────────────────────────
    Q_INVOKABLE int insertOperator(const QString &name, const QString &role);
    Q_INVOKABLE QList<QVariantMap> getAllOperators();
    Q_INVOKABLE bool updateOperatorStats(int operatorId, bool wasFlight);

    // ── Flight session (new flight record system) ──────────────────
    Q_INVOKABLE int openFlight(int operatorId, int vehicleId, const QString &mode,
                               const QString &purpose, const QString &location,
                               const QString &notes, const QString &weatherSummary);
    Q_INVOKABLE bool setFlightPreChecklistComplete(int flightId);
    Q_INVOKABLE bool setFlightPostChecklistComplete(int flightId);
    Q_INVOKABLE bool setFlightArmedAt(int flightId, const QDateTime &time);
    Q_INVOKABLE bool setFlightDisarmedAt(int flightId, const QDateTime &time);
    Q_INVOKABLE bool closeFlight(int flightId, int durationSec,
                                 double maxAltitude, double minBatteryV,
                                 double maxBatteryV, int modeChanges,
                                 double maxGroundSpeedMs = 0.0,
                                 double maxVerticalSpeedMs = 0.0,
                                 double distanceFlownM = 0.0,
                                 double avgBatteryV = 0.0,
                                 int checkPassCount = 0,
                                 int checkFailCount = 0,
                                 int checkWarnCount = 0,
                                 int anomalyCount = 0);
    Q_INVOKABLE QList<QVariantMap> getFlightsForVehicle(int vehicleId, int limit = 50);
    Q_INVOKABLE QVariantMap getFlightById(int flightId);

    // ── Flight history (list + detail) ─────────────────────────────
    /// Paginated, filtered flight list for the Flight History page.
    /// Filters: fromDate/toDate (YYYY-MM-DD, inclusive), vehicleId (0 = any),
    /// operatorId (0 = any), mode ("" = any), search (matches purpose,
    /// location, operator name, vehicle name).  sortBy: "date_desc" (default),
    /// "date_asc", "duration_desc", "operator", "vehicle", "pass_rate_desc".
    /// Returns { "rows": [...], "totalCount": N }.  Each row carries the full
    /// flights record plus operator_name and vehicle_name.
    Q_INVOKABLE QVariantMap queryFlights(int page, const QString &fromDate = QString(),
                                         const QString &toDate = QString(),
                                         int vehicleId = 0, int operatorId = 0,
                                         const QString &mode = QString(),
                                         const QString &search = QString(),
                                         const QString &sortBy = QStringLiteral("date_desc"),
                                         int pageSize = 50);

    /// Check results (pre/post) for a flight, grouped by category and ordered
    /// by evaluated time.  isPostFlight selects the audit section.
    Q_INVOKABLE QList<QVariantMap> getCheckResultsForFlight(int flightId, bool isPostFlight);
    /// Telemetry events for a flight in chronological order (arm → disarm),
    /// including lat/lon/HDOP/vertical speed/heading snapshot fields.
    Q_INVOKABLE QList<QVariantMap> getTelemetryEventsForFlight(int flightId);
    /// Trainer handover events recorded during a training session.
    Q_INVOKABLE QList<QVariantMap> getHandoverEventsForFlight(int flightId);
    /// Motor test audit rows for a vehicle (keyed by MAVLink sysid).
    Q_INVOKABLE QList<QVariantMap> getMotorTestsForVehicle(int vehicleSysId);
    /// Control-surface sweep test rows for a flight.
    Q_INVOKABLE QList<QVariantMap> getSurfaceTestsForFlight(int flightId);

    /// Exports the filtered flight list (same WHERE logic as queryFlights,
    /// minus pagination/search) to a CSV in the user's Documents folder.
    /// Returns the absolute path of the written file, or an empty string on
    /// failure.  vehicleId is the MAVLink sysid (0 = any); mode "" = any.
    Q_INVOKABLE QString exportFlightsCsv(const QString &fromDate = QString(),
                                         const QString &toDate = QString(),
                                         int vehicleId = 0,
                                         const QString &mode = QString());
    /// Exports every event, check result and test for ONE flight as a
    /// multi-section CSV (metadata, pre/post checks, telemetry events,
    /// handovers, motor tests, surface tests, zone compliance).
    /// Returns the absolute path of the written file, or an empty string on
    /// failure.
    Q_INVOKABLE QString exportFlightDetailCsv(int flightId);

    /// Aggregated statistics over the filtered flight set (same WHERE logic
    /// as queryFlights, minus pagination/search).  Returns a map with keys:
    /// totalFlights, totalHoursStr, avgPassRate, anomalyRate, vehicleCount,
    /// operatorCount.
    Q_INVOKABLE QVariantMap getFlightStats(const QString &fromDate = QString(),
                                           const QString &toDate = QString(),
                                           int vehicleId = 0,
                                           const QString &mode = QString());

    // ── Flight summary helpers ────────────────────────────────────
    /// Counts flight check results grouped by verdict (pass/fail/warn).
    /// Returns a map with integer keys "pass", "fail" and "warn".
    Q_INVOKABLE QVariantMap getCheckCountsForFlight(int flightId);
    /// Number of anomaly events (CHECK_DEGRADED / BATTERY_WARN) during a flight.
    Q_INVOKABLE int getAnomalyCountForFlight(int flightId);

    // ── Flight check results ───────────────────────────────────────
    Q_INVOKABLE bool insertFlightCheckResult(int flightId, const QString &checkId,
                                             const QString &category, bool isPostFlight,
                                             const QString &status, const QString &message,
                                             int confirmedBy = -1);

    // ── Flight telemetry events ─────────────────────────────────────
    Q_INVOKABLE bool insertTelemetryEvent(int flightId, const QString &eventType,
                                          const QString &triggeredBy, double batteryV,
                                          double altitudeM, int gpsSats,
                                          const QString &flightMode);
    /// Full telemetry snapshot write (also records GPS position, HDOP,
    /// vertical speed and heading so the flight detail view can show a
    /// richer timeline).  Basic fields kept in sync with insertTelemetryEvent.
    Q_INVOKABLE bool insertTelemetryEventSnapshot(int flightId, const QString &eventType,
                                                  const QString &triggeredBy, double batteryV,
                                                  double altitudeM, int gpsSats,
                                                  const QString &flightMode,
                                                  double latitude, double longitude,
                                                  double hdop, double verticalSpeed,
                                                  double headingDeg);

    // ── Transactions ──────────────────────────────────────────────
    /// Begin an explicit SQLite transaction (multi-table write atomicy).
    Q_INVOKABLE bool beginTransaction();
    Q_INVOKABLE bool commitTransaction();
    Q_INVOKABLE bool rollbackTransaction();

    // ── No-fly zone CRUD ────────────────────────────────────────────
    Q_INVOKABLE int insertZone(const QString &name, const QString &description,
                               double lat, double lon, double radiusM,
                               const QString &reason, int createdBy = -1);
    Q_INVOKABLE bool updateZone(int id, const QString &name, const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, bool active);
    Q_INVOKABLE bool deleteZone(int id);
    Q_INVOKABLE QList<QVariantMap> getAllZones();
    Q_INVOKABLE QList<QVariantMap> getActiveZones();
    /// Case-insensitive lookup by exact name.  Returns an empty map when no
    /// zone matches (used by NoFlyZoneModel's duplicate-name guard).
    Q_INVOKABLE QVariantMap getZoneByName(const QString &name);
    /// Seeds default sample restricted-airspace zones if table is empty.
    Q_INVOKABLE bool seedDefaultZonesIfNeeded();

    // ── Manual zone compliance logging ──────────────────────────────
    /// Writes one audit row.  When zoneId > 0 the row is tied to that zone and
    /// records whether the mission intersected it (per-zone compliance log).
    Q_INVOKABLE bool insertComplianceRecord(int flightId, int operatorId,
                                            const QString &result,
                                            const QString &notes,
                                            const QString &overrideReason,
                                            int zoneId = -1, bool intersecting = false);
    /// Stamps the operator's acknowledgement reason onto the newest log row
    /// for the given zone/flight pair.  Returns false when no row matches.
    Q_INVOKABLE bool updateComplianceOverrideReason(int zoneId, int flightId,
                                                    const QString &reason);
    Q_INVOKABLE QList<QVariantMap> getComplianceForFlight(int flightId);
    Q_INVOKABLE QList<QVariantMap> getComplianceHistory(int limit = 100);

    int schemaVersion() const;
    Q_INVOKABLE int storedSchemaVersion() const;
    Q_INVOKABLE bool migrateSchema();
    /// Closes flight records left open by a crash or power loss.  Called once
    /// at startup after migrations so interrupted sessions don't skew stats.
    bool recoverOrphanedSessions();
    void reset();

private:
    DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    /// Expands ~ in paths and ensures parent directories exist.
    QString expandPath(const QString &path);
    /// Creates all tables and indexes if they don't already exist.
    bool createTables();
    /// Executes a query, logging a warning with the table name on failure.
    bool execOrWarn(QSqlQuery &query, const char *tableName);
    /// Escapes special characters for safe embedding in JSON strings.
    QString escapeJson(const QString &raw);
    /// Copies the DB file to "<dbPath>.bak" before a schema migration so a
    /// failed migration can be rolled back manually.
    void backupBeforeMigration();
    /// Runs a passive WAL checkpoint; used by the periodic reliability timer.
    void walCheckpoint();
    /// Schedules the periodic WAL checkpoint (every 5 minutes).
    void startMaintenanceTimer();

    QSqlDatabase m_db;
    bool m_initialized = false;
    QTimer m_maintenanceTimer;
};
