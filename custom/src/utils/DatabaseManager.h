// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/DatabaseManager.h
// Description: SQLite wrapper for templates, compliance logs, vehicle/battery profiles.

#pragma once

// ============================================================================
// DatabaseManager — Singleton providing all SQLite persistence for the
// UAV Preflight Checklist plugin.  Owns a single .db file containing
// 13 tables that together cover:
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
//                                                by device UID (hardware UID
//                                                or fingerprint).
//                      vehicle_config          Per-vehicle check overrides as
//                                                a JSON blob.
//
//   Batteries           batteries               Master battery registry keyed
//                                                by serial number.
//                      battery_cycles          Per-flight cycle & health data.
//
//   Flights             flight_sessions         Start/end time, payload,
//                                                energy, and location per flight.
//
//   Check audit         check_results           Per-check pass/fail records
//                                                tied to a flight session.
//                      check_config            Per-check key/value overrides,
//                                                optionally scoped to a vehicle.
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

/// Lightweight representation of a row in the vehicle registry.
/// Used externally for serialisation; the DB methods return JSON strings
/// instead of this struct, so it mainly serves as documentation.
struct VehicleRecord {
    int sysid = 0;
    int compid = 0;
    QString fingerprint;
    QString autopilotType;
    QString vehicleType;
    QString firmwareVersion;
    quint64 uid = 0;
    QString boardVersion;
    QString displayName;
    QDateTime firstSeen;
    QDateTime lastSeen;
    int totalFlightCount = 0;
    double totalFlightHours = 0.0;
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
                                        const QString &firmwareVersion, quint64 uid,
                                        const QString &boardVersion, const QString &displayName);
    Q_INVOKABLE bool updateVehicleLastSeen(const QString &fingerprint);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);
    Q_INVOKABLE bool updateVehicleName(const QString &fingerprint, const QString &name);
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

    int schemaVersion() const;
    Q_INVOKABLE int storedSchemaVersion() const;
    Q_INVOKABLE bool migrateSchema();
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

    QSqlDatabase m_db;
    bool m_initialized = false;
};
