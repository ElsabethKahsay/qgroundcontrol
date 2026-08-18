// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/DatabaseManager.cpp
// Description: Implementation of SQLite storage for the pre-flight checklist.

#include "DatabaseManager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

/**
 * @brief Get the singleton instance of DatabaseManager
 * @return Reference to the singleton instance
 * 
 * Uses Meyers' singleton pattern for thread-safe lazy initialization.
 */
DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

/**
 * @brief Private constructor for singleton pattern
 * @param parent Optional parent QObject for memory management
 */
DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
}

/**
 * @brief Destructor - closes database connection
 */
DatabaseManager::~DatabaseManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

/**
 * @brief Reset database connection for testing/migration scenarios
 *
 * Closes the current connection and resets initialization state
 * so the next call to initialize() reconnects to a new database.
 */
void DatabaseManager::reset()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains()) {
        QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    }
    m_initialized = false;
}

/**
 * @brief Expand path to handle ~ and create directories
 * @param path Input path (may contain ~ for home directory)
 * @return Expanded absolute path
 * 
 * If path is empty, uses the default app data location.
 * Creates parent directories if they don't exist.
 */
QString DatabaseManager::expandPath(const QString &path)
{
    if (path.isEmpty()) {
        // Use default app data location if no path specified
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir dir(dataDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        return dir.filePath("uav_preflight_data.db");
    }
    
    // Expand ~ to home directory
    QString expanded = path;
    if (expanded.startsWith("~/")) {
        expanded.replace(0, 1, QDir::homePath());
    }
    
    // Create parent directories if they don't exist
    QFileInfo fi(expanded);
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    return expanded;
}

/**
 * @brief Initialize the database connection
 * @param dbPath Optional path to database file (empty = use default app data location)
 * @return true if initialization successful, false on error
 * 
 * Creates the SQLite database file if it doesn't exist and creates
 * the required tables (checklist_templates, compliance_logs).
 */
bool DatabaseManager::initialize(const QString &dbPath)
{
    if (m_initialized) return true;  // Already initialized

    QString finalPath = expandPath(dbPath);
    qDebug() << "DatabaseManager: Initializing SQLite at" << finalPath;

    // Add SQLite database connection
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(finalPath);

    if (!m_db.open()) {
        qWarning() << "DatabaseManager: Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // Create required tables
    if (!createTables()) {
        return false;
    }

    // Run pending schema migrations
    if (!migrateSchema()) {
        qWarning() << "DatabaseManager: schema migration failed";
        return false;
    }

    m_initialized = true;
    return true;
}

/**
 * @brief Create the required database tables
 * @return true if table creation successful, false on error
 * 
 * Creates two tables:
 * - checklist_templates: stores custom checklist templates
 * - compliance_logs: stores completed checklist compliance records
 * 
 * Also creates indexes on frequently queried columns for performance.
 */
// Schema versions:
//   1 - Initial: checklist_templates, compliance_logs, hardware_test_events, maintenance_components
//   2 - vehicles, batteries, battery_cycles, flight_sessions, check_results
//   3 - Phase 7: payload_weight_kg, location_name columns + indexes
//   4 - Phase 8/9: fingerprint columns, vehicle_config table
//   5 - plan_lat/plan_lon columns for per-plan location coordinates
//   6 - energy_consumed_wh/distance_m on flight_sessions; check_config table
//   7 - motor_test_results audit table
//   8 - Expanded vehicle profile columns (vehicle_uuid, frame_class, frame_type, motor_count, motor_layout, param_snapshot_path, last_preflight_status, gps_latitude, gps_longitude, pilot_name, notes, thumbnail)
//   9 - operators, flights, flight_check_results, flight_telemetry_events tables for flight record system
//  10 - no_fly_zones + zone_compliance_log tables for the airspace compliance system
static const int kLatestSchemaVersion = 11;

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    // ── Schema version tracking ─────────────────────────────────────────
    // Single-row table that tracks which migrations have been applied.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        )
    )");

    // checklist_templates table
    // Stores user-defined and built-in checklist templates, keyed by
    // template_id.  The UNIQUE constraint on (vehicle_type, template_name)
    // prevents duplicate names per vehicle category.  items_json holds the
    // full checklist as a JSON array of step objects.
    const QString createTemplates = R"(
        CREATE TABLE IF NOT EXISTS checklist_templates (
            template_id TEXT PRIMARY KEY,
            vehicle_type TEXT NOT NULL,
            template_name TEXT NOT NULL,
            template_version TEXT DEFAULT '1.0',
            items_json TEXT NOT NULL,
            is_default INTEGER DEFAULT 0,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            modified_at TEXT DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(vehicle_type, template_name)
        )
    )";

    if (!query.exec(createTemplates)) {
        qWarning() << "DatabaseManager: Failed to create checklist_templates table:" << query.lastError().text();
        return false;
    }

    // compliance_logs table
    // Records the full state of a completed checklist run for audit purposes.
    // checklist_json stores the serialized checklist with per-item results;
    // telemetry_snapshot captures the vehicle telemetry at the time of the run.
    const QString createLogs = R"(
        CREATE TABLE IF NOT EXISTS compliance_logs (
            log_id TEXT PRIMARY KEY,
            vehicle_id TEXT NOT NULL,
            vehicle_type TEXT NOT NULL,
            operator_id TEXT,
            operator_name TEXT,
            started_at TEXT,
            completed_at TEXT,
            overall_verdict TEXT,
            checklist_json TEXT NOT NULL,
            telemetry_snapshot TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";

    if (!query.exec(createLogs)) {
        qWarning() << "DatabaseManager: Failed to create compliance_logs table:" << query.lastError().text();
        return false;
    }

    // hardware_test_events table (for servo actuator testing)
    // Each row records one servo test step: the commanded PWM, observed
    // feedback, tolerance window, and whether the operator confirmed the
    // physical movement.  Non-fatal if creation fails (core tables still work).
    const QString createHardwareTests = R"(
        CREATE TABLE IF NOT EXISTS hardware_test_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            flight_id INTEGER NOT NULL,
            timestamp TEXT NOT NULL,
            checklist_item_id TEXT DEFAULT 'hardware_servo_test',
            step_name TEXT NOT NULL,
            servo_instance INTEGER NOT NULL,
            target_pwm INTEGER,
            feedback_pwm INTEGER,
            tolerance_min INTEGER,
            tolerance_max INTEGER,
            operator_confirmed INTEGER,
            result TEXT NOT NULL,
            failure_reason TEXT,
            operator_id TEXT,
            duration_ms INTEGER
        )
    )";

    if (!query.exec(createHardwareTests)) {
        // Non-fatal: core checklist/compliance tables remain usable without hardware audit.
        qWarning() << "DatabaseManager: Failed to create hardware_test_events table:"
                   << query.lastError().text();
    }

    // surface_test_results table (for control surface sweep testing)
    const QString createSurfaceTests = R"(
        CREATE TABLE IF NOT EXISTS surface_test_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            flight_id INTEGER REFERENCES flights(id),
            surface_id TEXT NOT NULL,
            channel INTEGER NOT NULL,
            min_pwm_actual INTEGER,
            max_pwm_actual INTEGER,
            direction_ok INTEGER,
            result TEXT NOT NULL,
            timestamp TEXT NOT NULL
        )
    )";
    query.exec(createSurfaceTests);

    // ── Indexes ──────────────────────────────────────────────────────
    query.exec("CREATE INDEX IF NOT EXISTS idx_hardware_flight ON hardware_test_events(flight_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_hardware_timestamp ON hardware_test_events(timestamp)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_vehicle ON compliance_logs(vehicle_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_date ON compliance_logs(created_at)");

    // maintenance_components table
    // Tracks replaceable parts (motors, props, batteries, ESCs) with
    // maximum hour and cycle limits.  MaintenanceTracker emits warnings
    // when currentHours/currentCycles approach the limits.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS maintenance_components (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            type TEXT NOT NULL,
            max_hours REAL NOT NULL DEFAULT 0,
            current_hours REAL NOT NULL DEFAULT 0,
            max_cycles INTEGER NOT NULL DEFAULT 0,
            current_cycles INTEGER NOT NULL DEFAULT 0,
            last_maintenance TEXT,
            notes TEXT
        )
    )");

    // ── vehicles table ───────────────────────────────────────────────
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS vehicles (
            device_uid TEXT PRIMARY KEY,
            friendly_name TEXT NOT NULL DEFAULT '',
            autopilot_type TEXT NOT NULL DEFAULT '',
            airframe_type TEXT NOT NULL DEFAULT '',
            first_seen TEXT NOT NULL,
            last_seen TEXT NOT NULL,
            total_flight_count INTEGER NOT NULL DEFAULT 0,
            total_flight_hours REAL NOT NULL DEFAULT 0.0,
            identity_source TEXT NOT NULL DEFAULT 'hardware_uid'
        )
    )");

    // ── batteries table (separate from vehicles) ─────────────────────
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS batteries (
            serial_number TEXT PRIMARY KEY,
            operator_label TEXT NOT NULL DEFAULT '',
            first_seen TEXT NOT NULL,
            last_seen TEXT NOT NULL,
            total_cycles INTEGER NOT NULL DEFAULT 0,
            identity_source TEXT NOT NULL DEFAULT 'operator_confirmed'
        )
    )");

    // ── battery_cycles table (FK → batteries) ────────────────────────
    // Per-flight battery health snapshot: capacity at full charge, voltage
    // sag under load, and resting voltage.  Used to compute health trends
    // over time and detect battery degradation.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS battery_cycles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            battery_serial TEXT NOT NULL REFERENCES batteries(serial_number),
            flight_session_id INTEGER NOT NULL DEFAULT 0,
            capacity_at_full_mah REAL NOT NULL DEFAULT 0.0,
            voltage_sag_v REAL NOT NULL DEFAULT 0.0,
            resting_voltage_v REAL NOT NULL DEFAULT 0.0,
            cycle_count INTEGER NOT NULL DEFAULT 0,
            recorded_at TEXT NOT NULL
        )
    )");

    // ── flight_sessions table (FK → vehicles) ────────────────────────
    // One row per flight: tracks start/end time, battery used, payload
    // weight, flight location, energy consumed, and distance traveled.
    // Extended across schema versions 3, 5, 6, and 8.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS flight_sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_uid TEXT NOT NULL REFERENCES vehicles(device_uid),
            battery_serial TEXT REFERENCES batteries(serial_number),
            started_at TEXT NOT NULL,
            ended_at TEXT,
            duration_seconds REAL NOT NULL DEFAULT 0.0
        )
    )");

    // ── check_results table (audit trail) ────────────────────────────
    // Records the outcome of each individual checklist item evaluation
    // during a flight session.  Provides the audit trail for compliance
    // reporting and export.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS check_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_uid TEXT NOT NULL REFERENCES vehicles(device_uid),
            flight_session_id INTEGER NOT NULL DEFAULT 0,
            check_id TEXT NOT NULL,
            status TEXT NOT NULL,
            message TEXT DEFAULT '',
            evaluated_at TEXT NOT NULL
        )
    )");

    // ── Phase 7: payload_weight_kg and location_name columns ──────────
    // Use ALTER TABLE ADD COLUMN with IF NOT EXISTS for idempotency
    // SQLite doesn't support IF NOT EXISTS for ALTER TABLE, so check column exists first
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA table_info(flight_sessions)");
    bool hasPayload = false;
    bool hasLocation = false;
    while (pragma.next()) {
        QString col = pragma.value(1).toString();
        if (col == QStringLiteral("payload_weight_kg"))
            hasPayload = true;
        if (col == QStringLiteral("location_name"))
            hasLocation = true;
    }
    if (!hasPayload) {
        query.exec("ALTER TABLE flight_sessions ADD COLUMN payload_weight_kg REAL NOT NULL DEFAULT 0.0");
    }
    if (!hasLocation) {
        query.exec("ALTER TABLE flight_sessions ADD COLUMN location_name TEXT DEFAULT ''");
    }

    // ── Phase 7 indexes ──────────────────────────────────────────────
    query.exec("CREATE INDEX IF NOT EXISTS idx_battery_cycles_serial ON battery_cycles(battery_serial)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_battery_cycles_session ON battery_cycles(flight_session_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_flight_sessions_vehicle ON flight_sessions(device_uid)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_check_results_session ON check_results(flight_session_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_check_results_vehicle ON check_results(device_uid)");

    // ── no_fly_zones table ───────────────────────────────────────────
    // Organizational knowledge base of known restricted airspace.  Purely a
    // persistence store — NO automatic geometric checks are performed against
    // missions; compliance is recorded manually via zone_compliance_log.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS no_fly_zones (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            radius_m REAL NOT NULL,
            reason TEXT NOT NULL DEFAULT 'Regulatory',
            active INTEGER NOT NULL DEFAULT 1,
            created_by INTEGER REFERENCES operators(id),
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )");

    // ── zone_compliance_log table ────────────────────────────────────
    // Audit trail of manual airspace compliance checks performed by the
    // operator before a mission proceeds.
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS zone_compliance_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            flight_id INTEGER REFERENCES flights(id),
            operator_id INTEGER REFERENCES operators(id),
            checked_at TEXT NOT NULL,
            result TEXT NOT NULL,
            notes TEXT,
            override_reason TEXT
        )
    )");

    query.exec("CREATE INDEX IF NOT EXISTS idx_zones_active ON no_fly_zones(active)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_flight ON zone_compliance_log(flight_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_checked ON zone_compliance_log(checked_at)");

    return true;
}

/**
 * @brief Save a checklist template to the database
 * @param templateId Unique template identifier
 * @param vehicleType Vehicle type (Quad, FixedWing, VTOL)
 * @param templateName Human-readable template name
 * @param templateJson JSON string containing checklist items
 * @return true if save successful, false on error
 * 
 * Uses INSERT OR REPLACE to update existing templates or create new ones.
 */
bool DatabaseManager::saveTemplate(const QString &templateId, const QString &vehicleType,
                                   const QString &templateName, const QString &templateJson)
{
    if (!m_initialized) return false;
    
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO checklist_templates "
                  "(template_id, vehicle_type, template_name, items_json, modified_at) "
                  "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)");
    query.addBindValue(templateId);
    query.addBindValue(vehicleType);
    query.addBindValue(templateName);
    query.addBindValue(templateJson);
    
    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to save template:" << query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief Load a checklist template from the database
 * @param templateId Unique template identifier
 * @return JSON string containing checklist items, or empty string if not found
 */
QString DatabaseManager::loadTemplate(const QString &templateId)
{
    if (!m_initialized) return QString();
    
    QSqlQuery query(m_db);
    query.prepare("SELECT items_json FROM checklist_templates WHERE template_id = ?");
    query.addBindValue(templateId);
    
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

/**
 * @brief List all template IDs for a vehicle type
 * @param vehicleType Vehicle type to filter by
 * @return List of template IDs
 */
QStringList DatabaseManager::listTemplates(const QString &vehicleType)
{
    QStringList result;
    if (!m_initialized) return result;
    
    QSqlQuery query(m_db);
    query.prepare("SELECT template_id FROM checklist_templates WHERE vehicle_type = ?");
    query.addBindValue(vehicleType);
    
    if (query.exec()) {
        while (query.next()) {
            result.append(query.value(0).toString());
        }
    }
    return result;
}

/**
 * @brief Delete a checklist template from the database
 * @param templateId Unique template identifier
 * @return true if deletion successful, false on error
 */
bool DatabaseManager::deleteTemplate(const QString &templateId)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM checklist_templates WHERE template_id = ?");
    query.addBindValue(templateId);
    return query.exec();
}

/**
 * @brief Save a compliance log to the database
 * @param logId Unique log identifier
 * @param vehicleId Vehicle identifier
 * @param vehicleType Vehicle type
 * @param operatorId Operator identifier
 * @param logJson JSON string containing checklist state
 * @param telemetrySnapshot JSON string containing telemetry snapshot
 * @return true if save successful, false on error
 */
bool DatabaseManager::saveComplianceLog(const QString &logId, const QString &vehicleId,
                                        const QString &vehicleType, const QString &operatorId,
                                        const QString &logJson, const QString &telemetrySnapshot)
{
    if (!m_initialized) return false;
    
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO compliance_logs "
                  "(log_id, vehicle_id, vehicle_type, operator_id, checklist_json, telemetry_snapshot) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(logId);
    query.addBindValue(vehicleId);
    query.addBindValue(vehicleType);
    query.addBindValue(operatorId);
    query.addBindValue(logJson);
    query.addBindValue(telemetrySnapshot);
    
    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to save compliance log:" << query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief Load a compliance log from the database
 * @param logId Unique log identifier
 * @return JSON string containing checklist state, or empty string if not found
 */
QString DatabaseManager::loadComplianceLog(const QString &logId)
{
    if (!m_initialized) return QString();
    
    QSqlQuery query(m_db);
    query.prepare("SELECT checklist_json FROM compliance_logs WHERE log_id = ?");
    query.addBindValue(logId);
    
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

/**
 * @brief List compliance logs for a vehicle
 * @param vehicleId Vehicle identifier to filter by
 * @param limit Maximum number of logs to return (default 50)
 * @return List of log IDs, ordered by creation date (newest first)
 */
QStringList DatabaseManager::listComplianceLogs(const QString &vehicleId, int limit)
{
    QStringList result;
    if (!m_initialized) return result;
    
    QSqlQuery query(m_db);
    query.prepare("SELECT log_id FROM compliance_logs WHERE vehicle_id = ? ORDER BY created_at DESC LIMIT ?");
    query.addBindValue(vehicleId);
    query.addBindValue(limit);
    
    if (query.exec()) {
        while (query.next()) {
            result.append(query.value(0).toString());
        }
    }
    return result;
}

/**
 * @brief Delete a compliance log from the database
 * @param logId Unique log identifier
 * @return true if deletion successful, false on error
 */
bool DatabaseManager::deleteComplianceLog(const QString &logId)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM compliance_logs WHERE log_id = ?");
    query.addBindValue(logId);
    return query.exec();
}

/**
 * @brief Log a hardware servo test step to the database
 */
bool DatabaseManager::logHardwareTestStep(
    int flightId,
    const QString &stepName,
    int servoInstance,
    int targetPwm,
    int feedbackPwm,
    int toleranceMin,
    int toleranceMax,
    bool operatorConfirmed,
    const QString &result,
    const QString &failureReason,
    const QString &operatorId,
    int durationMs)
{
    if (!m_initialized) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO hardware_test_events "
        "(flight_id, timestamp, step_name, servo_instance, target_pwm, feedback_pwm, "
        "tolerance_min, tolerance_max, operator_confirmed, result, failure_reason, operator_id, duration_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    query.addBindValue(flightId);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(stepName);
    query.addBindValue(servoInstance);
    query.addBindValue(targetPwm);
    query.addBindValue(feedbackPwm);
    query.addBindValue(toleranceMin);
    query.addBindValue(toleranceMax);
    query.addBindValue(operatorConfirmed ? 1 : 0);
    query.addBindValue(result);
    query.addBindValue(failureReason);
    query.addBindValue(operatorId);
    query.addBindValue(durationMs);

    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to log hardware test step:" << query.lastError().text();
        return false;
    }

    qDebug() << "DatabaseManager: Logged hardware test step -" << stepName << "result:" << result;
    return true;
}

/**
 * @brief Log a motor spin-up test result to the database
 *
 * Records the commanded throttle, expected vs. actual PWM, and the delta
 * between them.  Stored in the motor_test_results audit table (created in
 * schema v7).
 */
bool DatabaseManager::logMotorTestResult(int vehicleSysId, int motorIndex,
                                         int throttlePct, int durationSec,
                                         int expectedPwm, int actualPwm,
                                         int pwmDelta, const QString &result)
{
    if (!m_initialized) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO motor_test_results "
        "(vehicle_sys_id, motor_index, throttle_pct, duration_sec, expected_pwm, "
        " actual_pwm, pwm_delta, result, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(vehicleSysId);
    query.addBindValue(motorIndex);
    query.addBindValue(throttlePct);
    query.addBindValue(durationSec);
    query.addBindValue(expectedPwm);
    query.addBindValue(actualPwm);
    query.addBindValue(pwmDelta);
    query.addBindValue(result);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "DatabaseManager: logMotorTestResult failed:" << query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief Log a control surface test result to the database
 */
bool DatabaseManager::logSurfaceTestResult(int flightId, const QString &surfaceId, int channel,
                                           int minPwmActual, int maxPwmActual,
                                           int directionOk, const QString &result)
{
    if (!m_initialized) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO surface_test_results "
        "(flight_id, surface_id, channel, min_pwm_actual, max_pwm_actual, direction_ok, result, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(flightId);
    query.addBindValue(surfaceId);
    query.addBindValue(channel);
    query.addBindValue(minPwmActual);
    query.addBindValue(maxPwmActual);
    if (directionOk < 0) {
        query.addBindValue(QVariant(QMetaType::fromType<int>()));
    } else {
        query.addBindValue(directionOk);
    }
    query.addBindValue(result);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "DatabaseManager: logSurfaceTestResult failed:" << query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief Query hardware test events for a flight
 *
 * Returns a JSON array of all servo test steps for the given flight ID,
 * ordered by insertion order.  Each entry includes the PWM targets,
 * tolerances, operator confirmation, and result status.
 */
QString DatabaseManager::getHardwareTestEvents(int flightId)
{
    if (!m_initialized) return QString();

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT step_name, servo_instance, target_pwm, feedback_pwm, tolerance_min, tolerance_max, "
        "operator_confirmed, result, failure_reason, timestamp, duration_ms "
        "FROM hardware_test_events WHERE flight_id = ? ORDER BY id ASC"
    );
    query.addBindValue(flightId);

    if (!query.exec()) {
        qWarning() << "DatabaseManager: Failed to query hardware test events:" << query.lastError().text();
        return QString();
    }

    // Build JSON array of results
    QString json = "[";
    bool first = true;
    while (query.next()) {
        if (!first) json += ",";
        first = false;

        json += QString(
            "{"
            "\"step_name\":\"%1\","
            "\"servo_instance\":%2,"
            "\"target_pwm\":%3,"
            "\"feedback_pwm\":%4,"
            "\"tolerance_min\":%5,"
            "\"tolerance_max\":%6,"
            "\"operator_confirmed\":%7,"
            "\"result\":\"%8\","
            "\"failure_reason\":\"%9\","
            "\"timestamp\":\"%10\","
            "\"duration_ms\":%11"
            "}"
        )
        .arg(query.value(0).toString())
        .arg(query.value(1).toInt())
        .arg(query.value(2).toInt())
        .arg(query.value(3).toInt())
        .arg(query.value(4).toInt())
        .arg(query.value(5).toInt())
        .arg(query.value(6).toBool() ? "true" : "false")
        .arg(query.value(7).toString())
        .arg(query.value(8).toString())
        .arg(query.value(9).toString())
        .arg(query.value(10).toInt());
    }
    json += "]";

    return json;
}

/**
 * @brief Execute a query, logging a warning with the table name on failure
 * @return true if the query succeeded
 */
bool DatabaseManager::execOrWarn(QSqlQuery &query, const char *tableName)
{
    if (!query.exec()) {
        qWarning() << "DatabaseManager:" << tableName << "error:" << query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief Escape special characters for safe embedding in hand-built JSON strings
 */
QString DatabaseManager::escapeJson(const QString &raw)
{
    QString out = raw;
    out.replace('"', "\\\"");
    out.replace('\\', "\\\\");
    out.replace('\n', "\\n");
    out.replace('\r', "\\r");
    out.replace('\t', "\\t");
    return out;
}

/** @brief Returns the compile-time latest schema version (kLatestSchemaVersion). */
int DatabaseManager::schemaVersion() const
{
    return kLatestSchemaVersion;
}

/** @brief Returns the highest schema version recorded in the database. */
int DatabaseManager::storedSchemaVersion() const
{
    if (!m_initialized) return 0;
    QSqlQuery q(m_db);
    q.exec("SELECT MAX(version) FROM schema_version");
    if (q.next())
        return q.value(0).toInt();
    return 0;
}

/**
 * @brief Run incremental schema migrations from the stored version to latest.
 *
 * Each migration case adds columns or tables that didn't exist in the
 * previous version.  The version number is recorded after each successful
 * migration so that partially-applied upgrades can resume safely.
 */
bool DatabaseManager::migrateSchema()
{
    int stored = storedSchemaVersion();
    int latest = schemaVersion();
    if (stored >= latest)
        return true;

    qDebug() << "DatabaseManager: migrating schema from version" << stored << "to" << latest;

    QSqlQuery q(m_db);

    for (int v = stored + 1; v <= latest; ++v) {
        switch (v) {
        case 1:
        case 2:
        case 3:
            // Handled by createTables() — just record the version
            break;
        case 4: {
            // v4: fingerprint columns + vehicle_config table (Phase 8/9)
            QSqlQuery pragma(m_db);
            pragma.exec("PRAGMA table_info(vehicles)");
            bool hasFp = false;
            while (pragma.next()) {
                if (pragma.value(1).toString() == "fingerprint") { hasFp = true; break; }
            }
            if (!hasFp) {
                q.exec("ALTER TABLE vehicles ADD COLUMN fingerprint TEXT");
                q.exec("ALTER TABLE vehicles ADD COLUMN compid INTEGER DEFAULT 0");
                q.exec("ALTER TABLE vehicles ADD COLUMN firmware_version TEXT DEFAULT ''");
                q.exec("ALTER TABLE vehicles ADD COLUMN board_version TEXT DEFAULT ''");
            }
            q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_vehicles_fingerprint ON vehicles(fingerprint)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_vehicles_last_seen ON vehicles(last_seen)");
            q.exec("CREATE TABLE IF NOT EXISTS vehicle_config ("
                   "fingerprint TEXT PRIMARY KEY REFERENCES vehicles(fingerprint),"
                   "config_json TEXT NOT NULL DEFAULT '{}',"
                   "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            break;
        }
        case 5: {
            // v5: plan_lat/plan_lon columns for per-plan location coordinates
            QSqlQuery pragma5(m_db);
            pragma5.exec("PRAGMA table_info(flight_sessions)");
            bool hasLat = false;
            while (pragma5.next()) {
                if (pragma5.value(1).toString() == "plan_lat") { hasLat = true; break; }
            }
            if (!hasLat) {
                q.exec("ALTER TABLE flight_sessions ADD COLUMN plan_lat REAL DEFAULT 0.0");
                q.exec("ALTER TABLE flight_sessions ADD COLUMN plan_lon REAL DEFAULT 0.0");
            }
            break;
        }
        case 6: {
            // v6: energy_consumed_wh/distance_m on flight_sessions; check_config table
            QSqlQuery pragma6(m_db);
            pragma6.exec("PRAGMA table_info(flight_sessions)");
            bool hasEnergy = false;
            while (pragma6.next()) {
                if (pragma6.value(1).toString() == "energy_consumed_wh") { hasEnergy = true; break; }
            }
            if (!hasEnergy) {
                q.exec("ALTER TABLE flight_sessions ADD COLUMN energy_consumed_wh REAL DEFAULT 0.0");
                q.exec("ALTER TABLE flight_sessions ADD COLUMN distance_m REAL DEFAULT 0.0");
            }
            q.exec("CREATE TABLE IF NOT EXISTS check_config ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "vehicle_id INTEGER,"
                   "check_id TEXT NOT NULL,"
                   "key TEXT NOT NULL,"
                   "value TEXT NOT NULL,"
                   "updated_at TEXT NOT NULL)");
            q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_check_config_lookup "
                   "ON check_config(vehicle_id, check_id, key)");
            break;
        }
        case 7: {
            // v7: motor_test_results audit table
            q.exec("CREATE TABLE IF NOT EXISTS motor_test_results ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "vehicle_sys_id INTEGER NOT NULL,"
                   "motor_index INTEGER NOT NULL,"
                   "throttle_pct INTEGER NOT NULL,"
                   "duration_sec INTEGER NOT NULL,"
                   "expected_pwm INTEGER NOT NULL,"
                   "actual_pwm INTEGER NOT NULL,"
                   "pwm_delta INTEGER NOT NULL,"
                   "result TEXT NOT NULL,"
                   "timestamp TEXT NOT NULL)");
            break;
        }
        case 8: {
            // v8: expanded vehicle profile columns
            auto addCol = [&](const QString &colDef) {
                QSqlQuery pragma8(m_db);
                pragma8.exec("PRAGMA table_info(vehicles)");
                bool has = false;
                QString colName = colDef.section(' ', 0, 0);
                while (pragma8.next()) {
                    if (pragma8.value(1).toString() == colName) { has = true; break; }
                }
                if (!has) {
                    q.exec(QStringLiteral("ALTER TABLE vehicles ADD COLUMN %1").arg(colDef));
                }
            };
            addCol(QStringLiteral("vehicle_uuid TEXT DEFAULT ''"));
            addCol(QStringLiteral("frame_class INTEGER DEFAULT -1"));
            addCol(QStringLiteral("frame_type INTEGER DEFAULT -1"));
            addCol(QStringLiteral("motor_count INTEGER DEFAULT 0"));
            addCol(QStringLiteral("motor_layout TEXT DEFAULT ''"));
            addCol(QStringLiteral("param_snapshot_path TEXT DEFAULT ''"));
            addCol(QStringLiteral("last_preflight_status TEXT DEFAULT ''"));
            addCol(QStringLiteral("gps_latitude REAL DEFAULT 0.0"));
            addCol(QStringLiteral("gps_longitude REAL DEFAULT 0.0"));
            addCol(QStringLiteral("pilot_name TEXT DEFAULT ''"));
            addCol(QStringLiteral("notes TEXT DEFAULT ''"));
            addCol(QStringLiteral("thumbnail TEXT DEFAULT ''"));
            q.exec("CREATE INDEX IF NOT EXISTS idx_vehicles_airframe ON vehicles(airframe_type)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_vehicles_autopilot ON vehicles(autopilot_type)");
            break;
        }
        case 9: {
            // v9: operators, flights, flight_check_results, flight_telemetry_events tables
            q.exec("CREATE TABLE IF NOT EXISTS operators ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "name TEXT NOT NULL,"
                   "role TEXT NOT NULL DEFAULT 'Pilot',"
                   "created_at TEXT NOT NULL,"
                   "total_flights INTEGER NOT NULL DEFAULT 0,"
                   "total_training INTEGER NOT NULL DEFAULT 0)");
            q.exec("CREATE TABLE IF NOT EXISTS flights ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "operator_id INTEGER NOT NULL REFERENCES operators(id),"
                   "vehicle_id INTEGER NOT NULL REFERENCES vehicles(id),"
                   "mode TEXT NOT NULL,"
                   "purpose TEXT,"
                   "location TEXT,"
                   "notes TEXT,"
                   "weather_summary TEXT,"
                   "pre_checklist_complete INTEGER NOT NULL DEFAULT 0,"
                   "post_checklist_complete INTEGER NOT NULL DEFAULT 0,"
                   "started_at TEXT NOT NULL,"
                   "armed_at TEXT,"
                   "disarmed_at TEXT,"
                   "ended_at TEXT,"
                   "duration_sec INTEGER,"
                   "max_altitude_m REAL,"
                   "min_battery_v REAL,"
                   "max_battery_v REAL,"
                   "flight_mode_changes INTEGER NOT NULL DEFAULT 0)");
            q.exec("CREATE TABLE IF NOT EXISTS flight_check_results ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "flight_id INTEGER NOT NULL REFERENCES flights(id),"
                   "check_id TEXT NOT NULL,"
                   "category TEXT NOT NULL,"
                   "is_post_flight INTEGER NOT NULL DEFAULT 0,"
                   "status TEXT NOT NULL,"
                   "message TEXT,"
                   "confirmed_by INTEGER REFERENCES operators(id),"
                   "evaluated_at TEXT NOT NULL)");
            q.exec("CREATE TABLE IF NOT EXISTS flight_telemetry_events ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "flight_id INTEGER NOT NULL REFERENCES flights(id),"
                   "event_type TEXT NOT NULL,"
                   "triggered_by TEXT,"
                   "battery_v REAL,"
                   "altitude_m REAL,"
                   "gps_sats INTEGER,"
                   "flight_mode TEXT,"
                   "timestamp TEXT NOT NULL)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_flights_operator ON flights(operator_id)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_flights_vehicle ON flights(vehicle_id)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_flights_mode ON flights(mode)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_flight_check_results_flight ON flight_check_results(flight_id)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_events_flight ON flight_telemetry_events(flight_id)");
            break;
        }
        case 10: {
            // v10: no_fly_zones + zone_compliance_log tables (airspace compliance)
            q.exec("CREATE TABLE IF NOT EXISTS no_fly_zones ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "name TEXT NOT NULL,"
                   "description TEXT,"
                   "latitude REAL NOT NULL,"
                   "longitude REAL NOT NULL,"
                   "radius_m REAL NOT NULL,"
                   "reason TEXT NOT NULL DEFAULT 'Regulatory',"
                   "active INTEGER NOT NULL DEFAULT 1,"
                   "created_by INTEGER REFERENCES operators(id),"
                   "created_at TEXT NOT NULL,"
                   "updated_at TEXT NOT NULL)");
            q.exec("CREATE TABLE IF NOT EXISTS zone_compliance_log ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "flight_id INTEGER REFERENCES flights(id),"
                   "operator_id INTEGER REFERENCES operators(id),"
                   "checked_at TEXT NOT NULL,"
                   "result TEXT NOT NULL,"
                   "notes TEXT,"
                   "override_reason TEXT)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_zones_active ON no_fly_zones(active)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_compliance_flight ON zone_compliance_log(flight_id)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_compliance_checked ON zone_compliance_log(checked_at)");
            break;
        }
        case 11: {
            // v11: expanded vehicle identity + attribute columns for the
            // Vehicles page.  fingerprint_source records how each vehicle was
            // identified (HARDWARE_UID or SYSID_TYPE_FALLBACK); the remaining
            // columns track the human-readable vehicle type, raw hardware UID,
            // sysid, and accumulated flight time in seconds.
            auto addColV11 = [&](const QString &colDef) {
                QSqlQuery pragma11(m_db);
                pragma11.exec("PRAGMA table_info(vehicles)");
                bool has = false;
                QString colName = colDef.section(' ', 0, 0);
                while (pragma11.next()) {
                    if (pragma11.value(1).toString() == colName) { has = true; break; }
                }
                if (!has) {
                    q.exec(QStringLiteral("ALTER TABLE vehicles ADD COLUMN %1").arg(colDef));
                }
            };
            addColV11(QStringLiteral("fingerprint_source TEXT DEFAULT 'UNKNOWN'"));
            addColV11(QStringLiteral("vehicle_type_name TEXT DEFAULT ''"));
            addColV11(QStringLiteral("hardware_uid TEXT DEFAULT ''"));
            addColV11(QStringLiteral("sysid INTEGER DEFAULT 0"));
            addColV11(QStringLiteral("total_flight_time_sec INTEGER DEFAULT 0"));
            q.exec("CREATE INDEX IF NOT EXISTS idx_vehicles_sysid ON vehicles(sysid)");
            q.exec("CREATE INDEX IF NOT EXISTS idx_vehicles_source ON vehicles(fingerprint_source)");
            break;
        }
        default:
            qWarning() << "DatabaseManager: unknown migration version" << v;
            return false;
        }
        q.prepare("INSERT OR REPLACE INTO schema_version (version) VALUES (?)");
        q.addBindValue(v);
        if (!q.exec())
            qWarning() << "DatabaseManager: failed to record schema version" << v << q.lastError().text();
    }

    qDebug() << "DatabaseManager: schema migration complete, version" << latest;
    return true;
}

// ── Component maintenance ─────────────────────────────────────────────────
// CRUD operations for replaceable hardware components (motors, props,
// batteries, ESCs, etc.).  Each component tracks max hours and max
// cycles; MaintenanceTracker compares current usage against these limits
// to emit warning/critical signals.

bool DatabaseManager::addComponent(const QString &name, const QString &type,
                                    double maxHours, int maxCycles)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO maintenance_components "
                  "(name, type, max_hours, current_hours, max_cycles, current_cycles) "
                  "VALUES (?, ?, ?, 0, ?, 0)");
    query.addBindValue(name);
    query.addBindValue(type);
    query.addBindValue(maxHours);
    query.addBindValue(maxCycles);
    if (!query.exec()) {
        qWarning() << "DatabaseManager: addComponent failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateComponentHours(int id, double hours)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE maintenance_components SET current_hours = ? WHERE id = ?");
    query.addBindValue(hours);
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::incrementComponentCycle(int id)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE maintenance_components SET current_cycles = current_cycles + 1 WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::resetComponentMaintenance(int id, const QString &notes)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE maintenance_components SET current_hours = 0, current_cycles = 0, "
                  "last_maintenance = ?, notes = ? WHERE id = ?");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(notes);
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::deleteComponent(int id)
{
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM maintenance_components WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

QString DatabaseManager::listComponentsJson()
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery query(m_db);
    query.exec("SELECT id, name, type, max_hours, current_hours, max_cycles, "
               "current_cycles, last_maintenance, notes FROM maintenance_components ORDER BY id");

    QString json = QStringLiteral("[");
    bool first = true;
    while (query.next()) {
        if (!first) json += QStringLiteral(",");
        first = false;
        json += QStringLiteral(
            "{"
            "\"id\":%1,"
            "\"name\":\"%2\","
            "\"type\":\"%3\","
            "\"maxHours\":%4,"
            "\"currentHours\":%5,"
            "\"maxCycles\":%6,"
            "\"currentCycles\":%7,"
            "\"lastMaintenance\":\"%8\""
            "}")
            .arg(query.value(0).toInt())
            .arg(query.value(1).toString())
            .arg(query.value(2).toString())
            .arg(query.value(3).toDouble(), 0, 'f', 2)
            .arg(query.value(4).toDouble(), 0, 'f', 2)
            .arg(query.value(5).toInt())
            .arg(query.value(6).toInt())
            .arg(query.value(7).toString());
    }
    json += QStringLiteral("]");
    return json;
}

// ── Vehicle profile CRUD ────────────────────────────────────────────────────
// The vehicles table is the master registry.  upsertVehicle/upsertVehicleEx
// use INSERT ... ON CONFLICT to create or update in a single statement.
// "device_uid" is the hardware UID from the autopilot (e.g. PX4's SYS_UID);
// "fingerprint" is a connection-derived signature used for auto-identification.

bool DatabaseManager::upsertVehicle(const QString &deviceUid, const QString &friendlyName,
                                    const QString &autopilotType, const QString &airframeType)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO vehicles (device_uid, friendly_name, autopilot_type, airframe_type,
                              first_seen, last_seen, identity_source)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, 'hardware_uid')
        ON CONFLICT(device_uid) DO UPDATE SET
            friendly_name = CASE WHEN ? != '' THEN ? ELSE friendly_name END,
            autopilot_type = ?,
            airframe_type = ?,
            last_seen = CURRENT_TIMESTAMP
    )");
    q.addBindValue(deviceUid);
    q.addBindValue(friendlyName);
    q.addBindValue(autopilotType);
    q.addBindValue(airframeType);
    q.addBindValue(friendlyName);
    q.addBindValue(friendlyName);
    q.addBindValue(autopilotType);
    q.addBindValue(airframeType);
    return execOrWarn(q, "upsertVehicle");
}

bool DatabaseManager::upsertVehicleEx(const QString &deviceUid, const QString &friendlyName,
                                       const QString &autopilotType, const QString &airframeType,
                                       int frameClass, int frameType,
                                       int motorCount, const QString &motorLayout,
                                       double gpsLat, double gpsLon)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO vehicles (device_uid, friendly_name, autopilot_type, airframe_type,
                              frame_class, frame_type, motor_count, motor_layout,
                              gps_latitude, gps_longitude,
                              first_seen, last_seen, identity_source)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, 'hardware_uid')
        ON CONFLICT(device_uid) DO UPDATE SET
            friendly_name = CASE WHEN ? != '' THEN ? ELSE friendly_name END,
            autopilot_type = ?,
            airframe_type = ?,
            frame_class = CASE WHEN ? >= 0 THEN ? ELSE frame_class END,
            frame_type = CASE WHEN ? >= 0 THEN ? ELSE frame_type END,
            motor_count = CASE WHEN ? > 0 THEN ? ELSE motor_count END,
            motor_layout = CASE WHEN ? != '' THEN ? ELSE motor_layout END,
            gps_latitude = CASE WHEN ? != 0.0 OR ? != 0.0 THEN ? ELSE gps_latitude END,
            gps_longitude = CASE WHEN ? != 0.0 OR ? != 0.0 THEN ? ELSE gps_longitude END,
            last_seen = CURRENT_TIMESTAMP
    )");
    q.addBindValue(deviceUid);
    q.addBindValue(friendlyName);
    q.addBindValue(autopilotType);
    q.addBindValue(airframeType);
    q.addBindValue(frameClass);
    q.addBindValue(frameType);
    q.addBindValue(motorCount);
    q.addBindValue(motorLayout);
    q.addBindValue(gpsLat);
    q.addBindValue(gpsLon);
    // ON CONFLICT bind values
    q.addBindValue(friendlyName);
    q.addBindValue(friendlyName);
    q.addBindValue(autopilotType);
    q.addBindValue(airframeType);
    q.addBindValue(frameClass);
    q.addBindValue(frameClass);
    q.addBindValue(frameType);
    q.addBindValue(frameType);
    q.addBindValue(motorCount);
    q.addBindValue(motorCount);
    q.addBindValue(motorLayout);
    q.addBindValue(motorLayout);
    q.addBindValue(gpsLat);
    q.addBindValue(gpsLon);
    q.addBindValue(gpsLat);
    q.addBindValue(gpsLat);
    q.addBindValue(gpsLon);
    q.addBindValue(gpsLon);
    return execOrWarn(q, "upsertVehicleEx");
}

QString DatabaseManager::getVehicle(const QString &deviceUid)
{
    if (!m_initialized) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
              "first_seen, last_seen, total_flight_count, total_flight_hours, identity_source "
              "FROM vehicles WHERE device_uid = ?");
    q.addBindValue(deviceUid);
    if (!q.exec() || !q.next()) return {};
    return QStringLiteral(
        "{\"deviceUid\":\"%1\",\"friendlyName\":\"%2\",\"autopilotType\":\"%3\","
        "\"airframeType\":\"%4\",\"firstSeen\":\"%5\",\"lastSeen\":\"%6\","
        "\"totalFlightCount\":%7,\"totalFlightHours\":%8,\"identitySource\":\"%9\"}")
        .arg(escapeJson(q.value(0).toString()),
             escapeJson(q.value(1).toString()),
             escapeJson(q.value(2).toString()),
             escapeJson(q.value(3).toString()),
             escapeJson(q.value(4).toString()),
             escapeJson(q.value(5).toString()))
        .arg(q.value(6).toInt())
        .arg(q.value(7).toDouble(), 0, 'f', 2)
        .arg(escapeJson(q.value(8).toString()));
}

QStringList DatabaseManager::listVehicles()
{
    QStringList result;
    if (!m_initialized) return result;
    QSqlQuery q(m_db);
    q.exec("SELECT device_uid FROM vehicles ORDER BY last_seen DESC");
    while (q.next()) result.append(q.value(0).toString());
    return result;
}

bool DatabaseManager::deleteVehicle(const QString &deviceUid)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM vehicles WHERE device_uid = ?");
    q.addBindValue(deviceUid);
    return q.exec();
}

bool DatabaseManager::incrementFlightCount(const QString &deviceUid)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET total_flight_count = total_flight_count + 1, last_seen = CURRENT_TIMESTAMP WHERE device_uid = ?");
    q.addBindValue(deviceUid);
    return execOrWarn(q, "incrementFlightCount");
}

bool DatabaseManager::updateFlightHours(const QString &deviceUid, double hours)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET total_flight_hours = total_flight_hours + ?, last_seen = CURRENT_TIMESTAMP WHERE device_uid = ?");
    q.addBindValue(hours);
    q.addBindValue(deviceUid);
    return execOrWarn(q, "updateFlightHours");
}

bool DatabaseManager::updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                            const QString &notes)
{
    if (!m_initialized || deviceUid.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET pilot_name = ?, notes = ?, last_seen = CURRENT_TIMESTAMP WHERE device_uid = ?");
    q.addBindValue(pilotName);
    q.addBindValue(notes);
    q.addBindValue(deviceUid);
    return execOrWarn(q, "updateVehicleProfile");
}

bool DatabaseManager::updateVehicleGps(const QString &deviceUid, double lat, double lon)
{
    if (!m_initialized || deviceUid.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET gps_latitude = ?, gps_longitude = ?, last_seen = CURRENT_TIMESTAMP WHERE device_uid = ?");
    q.addBindValue(lat);
    q.addBindValue(lon);
    q.addBindValue(deviceUid);
    return execOrWarn(q, "updateVehicleGps");
}

bool DatabaseManager::updatePreflightStatus(const QString &deviceUid, const QString &status)
{
    if (!m_initialized || deviceUid.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET last_preflight_status = ?, last_seen = CURRENT_TIMESTAMP WHERE device_uid = ?");
    q.addBindValue(status);
    q.addBindValue(deviceUid);
    return execOrWarn(q, "updatePreflightStatus");
}

QString DatabaseManager::exportVehiclesJson()
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    q.exec("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
           "first_seen, last_seen, total_flight_count, total_flight_hours, "
           "compid, firmware_version, board_version, fingerprint, "
           "vehicle_uuid, frame_class, frame_type, motor_count, motor_layout, "
           "param_snapshot_path, last_preflight_status, "
           "gps_latitude, gps_longitude, pilot_name, notes, thumbnail "
           "FROM vehicles ORDER BY last_seen DESC");

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["deviceUid"] = q.value(0).toString();
        o["friendlyName"] = q.value(1).toString();
        o["autopilotType"] = q.value(2).toString();
        o["airframeType"] = q.value(3).toString();
        o["firstSeen"] = q.value(4).toString();
        o["lastSeen"] = q.value(5).toString();
        o["totalFlightCount"] = q.value(6).toInt();
        o["totalFlightHours"] = q.value(7).toDouble();
        o["compid"] = q.value(8).toInt();
        o["firmwareVersion"] = q.value(9).toString();
        o["boardVersion"] = q.value(10).toString();
        o["fingerprint"] = q.value(11).toString();
        o["vehicleUuid"] = q.value(12).toString();
        o["frameClass"] = q.value(13).toInt();
        o["frameType"] = q.value(14).toInt();
        o["motorCount"] = q.value(15).toInt();
        o["motorLayout"] = q.value(16).toString();
        o["paramSnapshotPath"] = q.value(17).toString();
        o["lastPreflightStatus"] = q.value(18).toString();
        o["gpsLatitude"] = q.value(19).toDouble();
        o["gpsLongitude"] = q.value(20).toDouble();
        o["pilotName"] = q.value(21).toString();
        o["notes"] = q.value(22).toString();
        o["thumbnail"] = q.value(23).toString();
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool DatabaseManager::importVehiclesJson(const QString &json)
{
    if (!m_initialized) return false;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return false;

    QJsonArray arr = doc.array();
    for (const QJsonValue &val : arr) {
        QJsonObject o = val.toObject();
        QString uid = o["deviceUid"].toString();
        if (uid.isEmpty()) continue;

        QSqlQuery q(m_db);
        q.prepare(R"(
            INSERT OR REPLACE INTO vehicles
            (device_uid, friendly_name, autopilot_type, airframe_type,
             first_seen, last_seen, total_flight_count, total_flight_hours,
             compid, firmware_version, board_version, fingerprint,
             vehicle_uuid, frame_class, frame_type, motor_count, motor_layout,
             param_snapshot_path, last_preflight_status,
             gps_latitude, gps_longitude, pilot_name, notes, thumbnail,
             identity_source)
            VALUES (?, ?, ?, ?,
                    ?, ?, ?, ?,
                    ?, ?, ?, ?,
                    ?, ?, ?, ?, ?,
                    ?, ?,
                    ?, ?, ?, ?, ?,
                    'import')
        )");
        q.addBindValue(uid);
        q.addBindValue(o["friendlyName"].toString());
        q.addBindValue(o["autopilotType"].toString());
        q.addBindValue(o["airframeType"].toString());
        q.addBindValue(o["firstSeen"].toString());
        q.addBindValue(o["lastSeen"].toString());
        q.addBindValue(o["totalFlightCount"].toInt());
        q.addBindValue(o["totalFlightHours"].toDouble());
        q.addBindValue(o["compid"].toInt());
        q.addBindValue(o["firmwareVersion"].toString());
        q.addBindValue(o["boardVersion"].toString());
        q.addBindValue(o["fingerprint"].toString());
        q.addBindValue(o["vehicleUuid"].toString());
        q.addBindValue(o["frameClass"].toInt());
        q.addBindValue(o["frameType"].toInt());
        q.addBindValue(o["motorCount"].toInt());
        q.addBindValue(o["motorLayout"].toString());
        q.addBindValue(o["paramSnapshotPath"].toString());
        q.addBindValue(o["lastPreflightStatus"].toString());
        q.addBindValue(o["gpsLatitude"].toDouble());
        q.addBindValue(o["gpsLongitude"].toDouble());
        q.addBindValue(o["pilotName"].toString());
        q.addBindValue(o["notes"].toString());
        q.addBindValue(o["thumbnail"].toString());
        if (!q.exec()) {
            qWarning() << "DatabaseManager: importVehiclesJson failed for" << uid << q.lastError().text();
        }
    }
    return true;
}

QString DatabaseManager::searchVehicles(const QString &query)
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    QString pattern = QStringLiteral("%%1%").arg(query);
    q.prepare("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
              "first_seen, last_seen, total_flight_count, total_flight_hours, "
              "fingerprint, vehicle_uuid, frame_class, frame_type, motor_count, "
              "last_preflight_status, pilot_name "
              "FROM vehicles WHERE "
              "friendly_name LIKE ? OR "
              "autopilot_type LIKE ? OR "
              "airframe_type LIKE ? OR "
              "pilot_name LIKE ? OR "
              "notes LIKE ? OR "
              "device_uid LIKE ? "
              "ORDER BY last_seen DESC LIMIT 50");
    for (int i = 0; i < 6; ++i)
        q.addBindValue(pattern);

    QJsonArray arr;
    if (q.exec()) {
        while (q.next()) {
            QJsonObject o;
            o["deviceUid"] = q.value(0).toString();
            o["friendlyName"] = q.value(1).toString();
            o["autopilotType"] = q.value(2).toString();
            o["airframeType"] = q.value(3).toString();
            o["firstSeen"] = q.value(4).toString();
            o["lastSeen"] = q.value(5).toString();
            o["totalFlightCount"] = q.value(6).toInt();
            o["totalFlightHours"] = q.value(7).toDouble();
            o["fingerprint"] = q.value(8).toString();
            o["vehicleUuid"] = q.value(9).toString();
            o["frameClass"] = q.value(10).toInt();
            o["frameType"] = q.value(11).toInt();
            o["motorCount"] = q.value(12).toInt();
            o["lastPreflightStatus"] = q.value(13).toString();
            o["pilotName"] = q.value(14).toString();
            arr.append(o);
        }
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ── Operator CRUD ───────────────────────────────────────────────────────────

int DatabaseManager::insertOperator(const QString &name, const QString &role)
{
    if (!m_initialized || name.trimmed().isEmpty()) return -1;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO operators (name, role, created_at) VALUES (?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(name.trimmed());
    q.addBindValue(role.isEmpty() ? QStringLiteral("Pilot") : role);
    if (!execOrWarn(q, "insertOperator")) return -1;
    return q.lastInsertId().toInt();
}

QList<QVariantMap> DatabaseManager::getAllOperators()
{
    QList<QVariantMap> result;
    if (!m_initialized) return result;
    QSqlQuery q(m_db);
    q.exec("SELECT id, name, role, total_flights, total_training FROM operators ORDER BY name ASC");
    while (q.next()) {
        QVariantMap row;
        row["id"] = q.value(0).toInt();
        row["name"] = q.value(1).toString();
        row["role"] = q.value(2).toString();
        row["total_flights"] = q.value(3).toInt();
        row["total_training"] = q.value(4).toInt();
        result.append(row);
    }
    return result;
}

bool DatabaseManager::updateOperatorStats(int operatorId, bool wasFlight)
{
    if (!m_initialized || operatorId <= 0) return false;
    QSqlQuery q(m_db);
    if (wasFlight) {
        q.prepare("UPDATE operators SET total_flights = total_flights + 1 WHERE id = ?");
    } else {
        q.prepare("UPDATE operators SET total_training = total_training + 1 WHERE id = ?");
    }
    q.addBindValue(operatorId);
    return execOrWarn(q, "updateOperatorStats");
}

// ── Flight session (new flight record system) ───────────────────────────────

int DatabaseManager::openFlight(int operatorId, int vehicleId, const QString &mode,
                                const QString &purpose, const QString &location,
                                const QString &notes, const QString &weatherSummary)
{
    if (!m_initialized || operatorId <= 0 || vehicleId <= 0) return -1;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO flights (operator_id, vehicle_id, mode, purpose, location, notes, "
              "weather_summary, started_at) VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(operatorId);
    q.addBindValue(vehicleId);
    q.addBindValue(mode);
    q.addBindValue(purpose);
    q.addBindValue(location);
    q.addBindValue(notes);
    q.addBindValue(weatherSummary);
    if (!execOrWarn(q, "openFlight")) return -1;
    return q.lastInsertId().toInt();
}

bool DatabaseManager::setFlightPreChecklistComplete(int flightId)
{
    if (!m_initialized || flightId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flights SET pre_checklist_complete = 1 WHERE id = ?");
    q.addBindValue(flightId);
    return execOrWarn(q, "setFlightPreChecklistComplete");
}

bool DatabaseManager::setFlightPostChecklistComplete(int flightId)
{
    if (!m_initialized || flightId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flights SET post_checklist_complete = 1 WHERE id = ?");
    q.addBindValue(flightId);
    return execOrWarn(q, "setFlightPostChecklistComplete");
}

bool DatabaseManager::setFlightArmedAt(int flightId, const QDateTime &time)
{
    if (!m_initialized || flightId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flights SET armed_at = ? WHERE id = ?");
    q.addBindValue(time.toString(Qt::ISODate));
    q.addBindValue(flightId);
    return execOrWarn(q, "setFlightArmedAt");
}

bool DatabaseManager::setFlightDisarmedAt(int flightId, const QDateTime &time)
{
    if (!m_initialized || flightId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flights SET disarmed_at = ? WHERE id = ?");
    q.addBindValue(time.toString(Qt::ISODate));
    q.addBindValue(flightId);
    return execOrWarn(q, "setFlightDisarmedAt");
}

bool DatabaseManager::closeFlight(int flightId, int durationSec,
                                  double maxAltitude, double minBatteryV,
                                  double maxBatteryV, int modeChanges)
{
    if (!m_initialized || flightId <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flights SET ended_at = CURRENT_TIMESTAMP, duration_sec = ?, "
              "max_altitude_m = ?, min_battery_v = ?, max_battery_v = ?, "
              "flight_mode_changes = ? WHERE id = ?");
    q.addBindValue(durationSec);
    q.addBindValue(maxAltitude);
    q.addBindValue(minBatteryV);
    q.addBindValue(maxBatteryV);
    q.addBindValue(modeChanges);
    q.addBindValue(flightId);
    return execOrWarn(q, "closeFlight");
}

QList<QVariantMap> DatabaseManager::getFlightsForVehicle(int vehicleId, int limit)
{
    QList<QVariantMap> result;
    if (!m_initialized || vehicleId <= 0) return result;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, operator_id, vehicle_id, mode, purpose, location, notes, "
              "weather_summary, pre_checklist_complete, post_checklist_complete, "
              "started_at, armed_at, disarmed_at, ended_at, duration_sec, "
              "max_altitude_m, min_battery_v, max_battery_v, flight_mode_changes "
              "FROM flights WHERE vehicle_id = ? ORDER BY started_at DESC LIMIT ?");
    q.addBindValue(vehicleId);
    q.addBindValue(limit);
    if (!q.exec()) return result;
    while (q.next()) {
        QVariantMap row;
        row["id"] = q.value(0).toInt();
        row["operator_id"] = q.value(1).toInt();
        row["vehicle_id"] = q.value(2).toInt();
        row["mode"] = q.value(3).toString();
        row["purpose"] = q.value(4).toString();
        row["location"] = q.value(5).toString();
        row["notes"] = q.value(6).toString();
        row["weather_summary"] = q.value(7).toString();
        row["pre_checklist_complete"] = q.value(8).toInt();
        row["post_checklist_complete"] = q.value(9).toInt();
        row["started_at"] = q.value(10).toString();
        row["armed_at"] = q.value(11).toString();
        row["disarmed_at"] = q.value(12).toString();
        row["ended_at"] = q.value(13).toString();
        row["duration_sec"] = q.value(14).toInt();
        row["max_altitude_m"] = q.value(15).toDouble();
        row["min_battery_v"] = q.value(16).toDouble();
        row["max_battery_v"] = q.value(17).toDouble();
        row["flight_mode_changes"] = q.value(18).toInt();
        result.append(row);
    }
    return result;
}

QVariantMap DatabaseManager::getFlightById(int flightId)
{
    QVariantMap row;
    if (!m_initialized || flightId <= 0) return row;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, operator_id, vehicle_id, mode, purpose, location, notes, "
              "weather_summary, pre_checklist_complete, post_checklist_complete, "
              "started_at, armed_at, disarmed_at, ended_at, duration_sec, "
              "max_altitude_m, min_battery_v, max_battery_v, flight_mode_changes "
              "FROM flights WHERE id = ?");
    q.addBindValue(flightId);
    if (!q.exec() || !q.next()) return row;
    row["id"] = q.value(0).toInt();
    row["operator_id"] = q.value(1).toInt();
    row["vehicle_id"] = q.value(2).toInt();
    row["mode"] = q.value(3).toString();
    row["purpose"] = q.value(4).toString();
    row["location"] = q.value(5).toString();
    row["notes"] = q.value(6).toString();
    row["weather_summary"] = q.value(7).toString();
    row["pre_checklist_complete"] = q.value(8).toInt();
    row["post_checklist_complete"] = q.value(9).toInt();
    row["started_at"] = q.value(10).toString();
    row["armed_at"] = q.value(11).toString();
    row["disarmed_at"] = q.value(12).toString();
    row["ended_at"] = q.value(13).toString();
    row["duration_sec"] = q.value(14).toInt();
    row["max_altitude_m"] = q.value(15).toDouble();
    row["min_battery_v"] = q.value(16).toDouble();
    row["max_battery_v"] = q.value(17).toDouble();
    row["flight_mode_changes"] = q.value(18).toInt();
    return row;
}

// ── Flight check results ────────────────────────────────────────────────────

bool DatabaseManager::insertFlightCheckResult(int flightId, const QString &checkId,
                                              const QString &category, bool isPostFlight,
                                              const QString &status, const QString &message,
                                              int confirmedBy)
{
    if (!m_initialized || flightId <= 0 || checkId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO flight_check_results (flight_id, check_id, category, "
              "is_post_flight, status, message, confirmed_by, evaluated_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(flightId);
    q.addBindValue(checkId);
    q.addBindValue(category);
    q.addBindValue(isPostFlight ? 1 : 0);
    q.addBindValue(status);
    q.addBindValue(message);
    if (confirmedBy > 0)
        q.addBindValue(confirmedBy);
    else
        q.addBindValue(QVariant());
    return execOrWarn(q, "insertFlightCheckResult");
}

// ── Flight telemetry events ─────────────────────────────────────────────────

bool DatabaseManager::insertTelemetryEvent(int flightId, const QString &eventType,
                                           const QString &triggeredBy, double batteryV,
                                           double altitudeM, int gpsSats,
                                           const QString &flightMode)
{
    if (!m_initialized || flightId <= 0 || eventType.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO flight_telemetry_events (flight_id, event_type, triggered_by, "
              "battery_v, altitude_m, gps_sats, flight_mode, timestamp) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(flightId);
    q.addBindValue(eventType);
    q.addBindValue(triggeredBy);
    q.addBindValue(batteryV);
    q.addBindValue(altitudeM);
    q.addBindValue(gpsSats);
    q.addBindValue(flightMode);
    return execOrWarn(q, "insertTelemetryEvent");
}

// ── Vehicle registry (fingerprint-based) ────────────────────────────────────

QString DatabaseManager::lookupVehicleByFingerprint(const QString &fingerprint)
{
    if (!m_initialized || fingerprint.isEmpty()) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
              "first_seen, last_seen, total_flight_count, total_flight_hours, "
              "compid, firmware_version, board_version, fingerprint "
              "FROM vehicles WHERE fingerprint = ?");
    q.addBindValue(fingerprint);
    if (!q.exec() || !q.next()) return {};

    QJsonObject o;
    o["deviceUid"] = QString::number(q.value(0).toULongLong());
    o["friendlyName"] = q.value(1).toString();
    o["autopilotType"] = q.value(2).toString();
    o["airframeType"] = q.value(3).toString();
    o["firstSeen"] = q.value(4).toString();
    o["lastSeen"] = q.value(5).toString();
    o["totalFlightCount"] = q.value(6).toInt();
    o["totalFlightHours"] = q.value(7).toDouble();
    o["compid"] = q.value(8).toInt();
    o["firmwareVersion"] = q.value(9).toString();
    o["boardVersion"] = q.value(10).toString();
    o["fingerprint"] = q.value(11).toString();
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool DatabaseManager::registerNewVehicle(const QString &fingerprint, int sysid, int compid,
                                          const QString &autopilotType, const QString &vehicleType,
                                          const QString &vehicleTypeName, const QString &firmwareVersion,
                                          quint64 uid, const QString &hardwareUid,
                                          const QString &boardVersion, const QString &displayName,
                                          const QString &fingerprintSource)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    // device_uid is the table PK; for hardware-resolved vehicles we key it to
    // the UID as before.  For sysid-fallback vehicles the UID is 0 and the raw
    // sysid alone would collide for two different vehicle types on the same
    // sysid (quad/fixed-wing SITL pair), so we key device_uid to the unique
    // fingerprint instead.
    const QString deviceUid = (uid != 0) ? QString::number(uid) : fingerprint;
    q.prepare(R"(
        INSERT INTO vehicles (device_uid, friendly_name, autopilot_type, airframe_type,
                              first_seen, last_seen, identity_source,
                              fingerprint, compid, firmware_version, board_version,
                              fingerprint_source, vehicle_type_name, hardware_uid, sysid)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, 'fingerprint',
                ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(fingerprint) DO UPDATE SET
            friendly_name = CASE WHEN ? != '' THEN ? ELSE friendly_name END,
            autopilot_type = ?,
            airframe_type = ?,
            firmware_version = ?,
            board_version = ?,
            fingerprint_source = ?,
            vehicle_type_name = ?,
            hardware_uid = ?,
            sysid = ?,
            last_seen = CURRENT_TIMESTAMP
    )");
    q.addBindValue(deviceUid);
    q.addBindValue(displayName);
    q.addBindValue(autopilotType);
    q.addBindValue(vehicleType);
    q.addBindValue(fingerprint);
    q.addBindValue(compid);
    q.addBindValue(firmwareVersion);
    q.addBindValue(boardVersion);
    q.addBindValue(fingerprintSource);
    q.addBindValue(vehicleTypeName);
    q.addBindValue(hardwareUid);
    q.addBindValue(sysid);
    q.addBindValue(displayName);
    q.addBindValue(displayName);
    q.addBindValue(autopilotType);
    q.addBindValue(vehicleType);
    q.addBindValue(firmwareVersion);
    q.addBindValue(boardVersion);
    q.addBindValue(fingerprintSource);
    q.addBindValue(vehicleTypeName);
    q.addBindValue(hardwareUid);
    q.addBindValue(sysid);
    return execOrWarn(q, "registerNewVehicle");
}

bool DatabaseManager::updateVehicleLastSeen(const QString &fingerprint)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET last_seen = CURRENT_TIMESTAMP WHERE fingerprint = ?");
    q.addBindValue(fingerprint);
    return execOrWarn(q, "updateVehicleLastSeen");
}

bool DatabaseManager::updateVehicleName(const QString &fingerprint, const QString &name)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET friendly_name = ? WHERE fingerprint = ?");
    q.addBindValue(name);
    q.addBindValue(fingerprint);
    return execOrWarn(q, "updateVehicleName");
}

bool DatabaseManager::updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE vehicles SET firmware_version = ?, last_seen = CURRENT_TIMESTAMP WHERE fingerprint = ?");
    q.addBindValue(firmwareVersion);
    q.addBindValue(fingerprint);
    return execOrWarn(q, "updateVehicleFirmware");
}

/** @brief Re-keys a vehicle to a new (better) fingerprint, e.g. when the
 *         hardware UID arrives and upgrades a sysid-based fallback identity. */
bool DatabaseManager::updateVehicleFingerprint(const QString &oldFingerprint, const QString &newFingerprint,
                                               const QString &newSource)
{
    if (!m_initialized || oldFingerprint.isEmpty() || newFingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE vehicles
        SET fingerprint = ?, fingerprint_source = ?,
            device_uid = CASE WHEN device_uid = ? THEN ? ELSE device_uid END
        WHERE fingerprint = ? AND fingerprint_source = 'SYSID_TYPE_FALLBACK'
    )");
    q.addBindValue(newFingerprint);
    q.addBindValue(newSource);
    q.addBindValue(oldFingerprint);
    q.addBindValue(newFingerprint);
    q.addBindValue(oldFingerprint);
    return execOrWarn(q, "updateVehicleFingerprint");
}

/** @brief Refreshes the live attribute columns for a known fingerprint.
 *         Called on every connect so the Vehicles page always reflects the
 *         current firmware, airframe and autopilot details. */
bool DatabaseManager::updateVehicleAttributes(const QString &fingerprint, const QString &autopilotType,
                                              const QString &vehicleType, const QString &vehicleTypeName,
                                              const QString &firmwareVersion, const QString &hardwareUid,
                                              int sysid, const QString &boardVersion,
                                              int frameClass, int motorCount)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE vehicles SET
            autopilot_type = ?,
            airframe_type = ?,
            vehicle_type_name = ?,
            firmware_version = COALESCE(NULLIF(?, ''), firmware_version),
            hardware_uid = ?,
            sysid = ?,
            board_version = ?,
            frame_class = ?,
            motor_count = ?,
            identity_source = 'fingerprint',
            last_seen = CURRENT_TIMESTAMP
        WHERE fingerprint = ?
    )");
    q.addBindValue(autopilotType);
    q.addBindValue(vehicleType);
    q.addBindValue(vehicleTypeName);
    q.addBindValue(firmwareVersion);
    q.addBindValue(hardwareUid);
    q.addBindValue(sysid);
    q.addBindValue(boardVersion);
    q.addBindValue(frameClass);
    q.addBindValue(motorCount);
    q.addBindValue(fingerprint);
    return execOrWarn(q, "updateVehicleAttributes");
}

/** @brief Accumulates flight time onto a vehicle's totals. Called by
 *         FlightSession::closeSession() so the Vehicles page shows real
 *         accumulated flight time per airframe. */
bool DatabaseManager::incrementVehicleFlightTime(const QString &fingerprint, int durationSeconds)
{
    if (!m_initialized || fingerprint.isEmpty() || durationSeconds <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE vehicles SET
            total_flight_time_sec = total_flight_time_sec + ?,
            total_flight_count = total_flight_count + 1,
            total_flight_hours = (total_flight_time_sec + ?) / 3600.0
        WHERE fingerprint = ?
    )");
    q.addBindValue(durationSeconds);
    q.addBindValue(durationSeconds);
    q.addBindValue(fingerprint);
    return execOrWarn(q, "incrementVehicleFlightTime");
}

QString DatabaseManager::getAllVehiclesJson()
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    q.exec("SELECT device_uid, friendly_name, autopilot_type, airframe_type, "
           "first_seen, last_seen, total_flight_count, total_flight_hours, "
           "compid, firmware_version, board_version, fingerprint, "
           "fingerprint_source, vehicle_type_name, hardware_uid, sysid, "
           "frame_class, motor_count, total_flight_time_sec, notes "
           "FROM vehicles ORDER BY last_seen DESC");

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["deviceUid"] = QString::number(q.value(0).toULongLong());
        o["friendlyName"] = q.value(1).toString();
        o["autopilotType"] = q.value(2).toString();
        o["airframeType"] = q.value(3).toString();
        o["firstSeen"] = q.value(4).toString();
        o["lastSeen"] = q.value(5).toString();
        o["totalFlightCount"] = q.value(6).toInt();
        o["totalFlightHours"] = q.value(7).toDouble();
        o["compid"] = q.value(8).toInt();
        o["firmwareVersion"] = q.value(9).toString();
        o["boardVersion"] = q.value(10).toString();
        o["fingerprint"] = q.value(11).toString();
        o["fingerprintSource"] = q.value(12).toString();
        o["vehicleTypeName"] = q.value(13).toString();
        o["hardwareUid"] = q.value(14).toString();
        o["sysid"] = q.value(15).toInt();
        o["frameClass"] = q.value(16).toInt();
        o["motorCount"] = q.value(17).toInt();
        o["totalFlightTimeSec"] = q.value(18).toInt();
        o["notes"] = q.value(19).toString();
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

static QString csvQuote(const QString &value)
{
    QString v = value;
    if (v.contains(',') || v.contains('"') || v.contains('\n') || v.contains('\r'))
        return QStringLiteral("\"%1\"").arg(QString(v).replace(QStringLiteral("\""), QStringLiteral("\"\"")));
    return v;
}

/** @brief Exports the vehicle registry to a CSV file in the user's Documents
 *         folder, optionally filtered by a last-seen date range (YYYY-MM-DD).
 * @return absolute path of the written file, or an empty string on failure. */
QString DatabaseManager::exportVehiclesCsv(const QString &fromDate, const QString &toDate)
{
    if (!m_initialized) return {};

    QString query = "SELECT device_uid, friendly_name, vehicle_type_name, autopilot_type, "
                    "firmware_version, hardware_uid, fingerprint, fingerprint_source, sysid, "
                    "motor_count, frame_class, first_seen, last_seen, total_flight_count, "
                    "total_flight_time_sec, notes "
                    "FROM vehicles WHERE 1=1";
    QVariantList binds;
    if (!fromDate.isEmpty()) {
        query += " AND last_seen >= ?";
        binds << QStringLiteral("%1 00:00:00").arg(fromDate);
    }
    if (!toDate.isEmpty()) {
        query += " AND last_seen <= ?";
        binds << QStringLiteral("%1 23:59:59").arg(toDate);
    }
    query += " ORDER BY last_seen DESC";

    QSqlQuery q(m_db);
    q.prepare(query);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: exportVehiclesCsv query failed:" << q.lastError().text();
        return {};
    }

    QStringList lines;
    lines << QStringLiteral(
        "ID,Display Name,Vehicle Type,Autopilot,Firmware Version,Hardware UID,Fingerprint,"
        "Fingerprint Source,SysID,Motor Count,Frame Class,First Seen,Last Seen,Total Flights,"
        "Total Flight Time (sec),Notes");
    while (q.next())
        lines << QStringList{
            csvQuote(q.value(0).toString()), csvQuote(q.value(1).toString()),
            csvQuote(q.value(2).toString()), csvQuote(q.value(3).toString()),
            csvQuote(q.value(4).toString()), csvQuote(q.value(5).toString()),
            csvQuote(q.value(6).toString()), csvQuote(q.value(7).toString()),
            QString::number(q.value(8).toInt()), QString::number(q.value(9).toInt()),
            QString::number(q.value(10).toInt()),
            csvQuote(q.value(11).toString()), csvQuote(q.value(12).toString()),
            QString::number(q.value(13).toInt()), QString::number(q.value(14).toInt()),
            csvQuote(q.value(15).toString())
        }.join(',');

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty()) return {};
    QDir d(dir);
    if (!d.exists()) d.mkpath(dir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = QStringLiteral("%1/skywin_vehicles_%2.csv").arg(dir, stamp);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DatabaseManager: exportVehiclesCsv cannot write" << path;
        return {};
    }
    f.write(lines.join('\n').toUtf8());
    f.close();
    qDebug() << "DatabaseManager: exported" << lines.size() - 1 << "vehicles to" << path;
    return path;
}

// ── Vehicle check config ─────────────────────────────────────────────────────

bool DatabaseManager::saveVehicleConfig(const QString &fingerprint, const QString &configJson)
{
    if (!m_initialized || fingerprint.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO vehicle_config (fingerprint, config_json, updated_at)
        VALUES (?, ?, CURRENT_TIMESTAMP)
        ON CONFLICT(fingerprint) DO UPDATE SET
            config_json = ?,
            updated_at = CURRENT_TIMESTAMP
    )");
    q.addBindValue(fingerprint);
    q.addBindValue(configJson);
    q.addBindValue(configJson);
    return execOrWarn(q, "saveVehicleConfig");
}

QString DatabaseManager::loadVehicleConfig(const QString &fingerprint)
{
    if (!m_initialized || fingerprint.isEmpty()) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT config_json FROM vehicle_config WHERE fingerprint = ?");
    q.addBindValue(fingerprint);
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

// ── Battery CRUD ────────────────────────────────────────────────────────────
// Batteries are tracked independently of vehicles since they can be
// swapped.  Each battery is identified by serial number and accumulates
// a total cycle count across all vehicles it has been used in.

bool DatabaseManager::upsertBattery(const QString &serialNumber, const QString &operatorLabel)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO batteries (serial_number, operator_label, first_seen, last_seen)
        VALUES (?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
        ON CONFLICT(serial_number) DO UPDATE SET
            operator_label = CASE WHEN ? != '' THEN ? ELSE operator_label END,
            last_seen = CURRENT_TIMESTAMP
    )");
    q.addBindValue(serialNumber);
    q.addBindValue(operatorLabel);
    q.addBindValue(operatorLabel);
    q.addBindValue(operatorLabel);
    return execOrWarn(q, "upsertBattery");
}

QString DatabaseManager::getBattery(const QString &serialNumber)
{
    if (!m_initialized) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT serial_number, operator_label, first_seen, last_seen, total_cycles, identity_source "
              "FROM batteries WHERE serial_number = ?");
    q.addBindValue(serialNumber);
    if (!q.exec() || !q.next()) return {};
    return QStringLiteral(
        "{\"serialNumber\":\"%1\",\"operatorLabel\":\"%2\",\"firstSeen\":\"%3\","
        "\"lastSeen\":\"%4\",\"totalCycles\":%5,\"identitySource\":\"%6\"}")
        .arg(escapeJson(q.value(0).toString()),
             escapeJson(q.value(1).toString()),
             escapeJson(q.value(2).toString()),
             escapeJson(q.value(3).toString()))
        .arg(q.value(4).toInt())
        .arg(escapeJson(q.value(5).toString()));
}

QStringList DatabaseManager::listBatteries()
{
    QStringList result;
    if (!m_initialized) return result;
    QSqlQuery q(m_db);
    q.exec("SELECT serial_number FROM batteries ORDER BY last_seen DESC");
    while (q.next()) result.append(q.value(0).toString());
    return result;
}

// ── Battery cycle CRUD ──────────────────────────────────────────────────────
// Each row records a single flight's battery health snapshot.
// getBatteryHealthTrend compares the earliest and latest capacities
// to estimate degradation percentage.

bool DatabaseManager::saveBatteryCycle(const QString &serialNumber, int flightSessionId,
                                       double capacityAtFullMah, double voltageSagV,
                                       double restingVoltageV, int cycleCount)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO battery_cycles (battery_serial, flight_session_id, capacity_at_full_mah,
                                    voltage_sag_v, resting_voltage_v, cycle_count, recorded_at)
        VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )");
    q.addBindValue(serialNumber);
    q.addBindValue(flightSessionId);
    q.addBindValue(capacityAtFullMah);
    q.addBindValue(voltageSagV);
    q.addBindValue(restingVoltageV);
    q.addBindValue(cycleCount);
    if (!execOrWarn(q, "saveBatteryCycle")) return false;

    // Update battery total_cycles
    QSqlQuery u(m_db);
    u.prepare("UPDATE batteries SET total_cycles = total_cycles + 1 WHERE serial_number = ?");
    u.addBindValue(serialNumber);
    u.exec();
    return true;
}

QString DatabaseManager::getBatteryCycles(const QString &serialNumber, int limit)
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    q.prepare("SELECT id, flight_session_id, capacity_at_full_mah, voltage_sag_v, "
              "resting_voltage_v, cycle_count, recorded_at "
              "FROM battery_cycles WHERE battery_serial = ? ORDER BY id DESC LIMIT ?");
    q.addBindValue(serialNumber);
    q.addBindValue(limit);
    if (!q.exec()) return QStringLiteral("[]");

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["id"] = q.value(0).toInt();
        o["flightSessionId"] = q.value(1).toInt();
        o["capacityAtFullMah"] = q.value(2).toDouble();
        o["voltageSagV"] = q.value(3).toDouble();
        o["restingVoltageV"] = q.value(4).toDouble();
        o["cycleCount"] = q.value(5).toInt();
        o["recordedAt"] = q.value(6).toString();
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

int DatabaseManager::getBatteryCycleCount(const QString &serialNumber)
{
    if (!m_initialized) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM battery_cycles WHERE battery_serial = ?");
    q.addBindValue(serialNumber);
    if (!q.exec()) return 0;
    if (q.next()) return q.value(0).toInt();
    return 0;
}

/**
 * @brief Record a battery cycle marker for a completed flight session.
 *
 * Only counts as a cycle if the session lasted >= 30 seconds and no
 * cycle has already been recorded for that session (prevents double-counting).
 * Inserts a lightweight marker with zeroed health data; callers should
 * use saveBatteryCycle() for detailed health snapshots.
 */
bool DatabaseManager::incrementBatteryCycle(int flightSessionId, const QString &batterySerial)
{
    if (!m_initialized) return false;

    // Check session exists and has duration >= 30s
    QSqlQuery q(m_db);
    q.prepare("SELECT duration_seconds, battery_serial FROM flight_sessions WHERE id = ?");
    q.addBindValue(flightSessionId);
    if (!q.exec() || !q.next()) return false;
    double durationSec = q.value(0).toDouble();
    if (durationSec < 30.0) return false;

    // Guard: only one cycle mark per session
    QSqlQuery check(m_db);
    check.prepare("SELECT COUNT(*) FROM battery_cycles WHERE flight_session_id = ?");
    check.addBindValue(flightSessionId);
    if (check.exec() && check.next() && check.value(0).toInt() > 0) return false;

    QString serial = batterySerial.isEmpty() ? q.value(1).toString() : batterySerial;
    if (serial.isEmpty()) serial = QStringLiteral("unknown");

    // Insert lightweight cycle marker
    QSqlQuery ins(m_db);
    ins.prepare(R"(
        INSERT INTO battery_cycles (battery_serial, flight_session_id, capacity_at_full_mah,
                                    voltage_sag_v, resting_voltage_v, cycle_count, recorded_at)
        VALUES (?, ?, 0.0, 0.0, 0.0, 1, CURRENT_TIMESTAMP)
    )");
    ins.addBindValue(serial);
    ins.addBindValue(flightSessionId);
    if (!execOrWarn(ins, "incrementBatteryCycle")) return false;

    // Update battery aggregate
    QSqlQuery u(m_db);
    u.prepare("UPDATE batteries SET total_cycles = total_cycles + 1 WHERE serial_number = ?");
    u.addBindValue(serial);
    u.exec();

    return true;
}

/**
 * @brief Compute battery degradation trend from all recorded cycles.
 *
 * Returns a JSON object with baseline/latest capacity, retained percentage,
 * average voltage sag, average resting voltage, and total cycle count.
 * Compares the first recorded capacity against the most recent to
 * estimate overall health degradation.
 */
QString DatabaseManager::getBatteryHealthTrend(const QString &serialNumber)
{
    if (!m_initialized) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT capacity_at_full_mah, voltage_sag_v, resting_voltage_v, cycle_count "
              "FROM battery_cycles WHERE battery_serial = ? ORDER BY id ASC");
    q.addBindValue(serialNumber);
    if (!q.exec()) return {};

    double baselineCapacity = -1.0;
    double latestCapacity = -1.0;
    double totalSag = 0.0;
    int sagCount = 0;
    double totalResting = 0.0;
    int restingCount = 0;
    int totalCycles = 0;

    while (q.next()) {
        double cap = q.value(0).toDouble();
        double sag = q.value(1).toDouble();
        double rest = q.value(2).toDouble();
        totalCycles = q.value(3).toInt();

        if (baselineCapacity < 0) baselineCapacity = cap;
        latestCapacity = cap;
        if (sag > 0) { totalSag += sag; sagCount++; }
        if (rest > 0) { totalResting += rest; restingCount++; }
    }

    if (baselineCapacity < 0) return {};

    double retainedPct = baselineCapacity > 0 ? (latestCapacity / baselineCapacity) * 100.0 : 100.0;
    double avgSag = sagCount > 0 ? totalSag / sagCount : 0.0;
    double avgResting = restingCount > 0 ? totalResting / restingCount : 0.0;

    return QStringLiteral(
        "{\"baselineCapacityMah\":%1,\"latestCapacityMah\":%2,"
        "\"capacityRetainedPct\":%3,\"averageVoltageSagV\":%4,"
        "\"averageRestingVoltageV\":%5,\"totalCycles\":%6}")
        .arg(baselineCapacity, 0, 'f', 1)
        .arg(latestCapacity, 0, 'f', 1)
        .arg(retainedPct, 0, 'f', 1)
        .arg(avgSag, 0, 'f', 3)
        .arg(avgResting, 0, 'f', 3)
        .arg(totalCycles);
}

// ── Flight session CRUD ─────────────────────────────────────────────────────

int DatabaseManager::startFlightSession(const QString &deviceUid, const QString &batterySerial,
                                        double payloadWeightKg)
{
    if (!m_initialized) return -1;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO flight_sessions (device_uid, battery_serial, payload_weight_kg, started_at) "
              "VALUES (?, ?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(deviceUid);
    q.addBindValue(batterySerial);
    q.addBindValue(payloadWeightKg);
    if (!execOrWarn(q, "startFlightSession")) return -1;
    return q.lastInsertId().toInt();
}

bool DatabaseManager::updateFlightSessionPayload(int sessionId, double payloadWeightKg)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET payload_weight_kg = ? WHERE id = ?");
    q.addBindValue(payloadWeightKg);
    q.addBindValue(sessionId);
    return execOrWarn(q, "updateFlightSessionPayload");
}

bool DatabaseManager::updateFlightSessionLocation(int sessionId, const QString &locationName)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET location_name = ? WHERE id = ?");
    q.addBindValue(locationName);
    q.addBindValue(sessionId);
    return execOrWarn(q, "updateFlightSessionLocation");
}

bool DatabaseManager::updateFlightSessionPlanLocation(int sessionId, double lat, double lon)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET plan_lat = ?, plan_lon = ? WHERE id = ?");
    q.addBindValue(lat);
    q.addBindValue(lon);
    q.addBindValue(sessionId);
    return execOrWarn(q, "updateFlightSessionPlanLocation");
}

bool DatabaseManager::endFlightSession(int sessionId, double durationSeconds)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET ended_at = CURRENT_TIMESTAMP, duration_seconds = ? WHERE id = ?");
    q.addBindValue(durationSeconds);
    q.addBindValue(sessionId);
    return execOrWarn(q, "endFlightSession");
}

bool DatabaseManager::updateFlightSessionEnergy(int sessionId, double energyConsumedWh, double distanceM)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET energy_consumed_wh = ?, distance_m = ? WHERE id = ?");
    q.addBindValue(energyConsumedWh);
    q.addBindValue(distanceM);
    q.addBindValue(sessionId);
    return execOrWarn(q, "updateFlightSessionEnergy");
}

QString DatabaseManager::getFlightSessions(const QString &deviceUid, int limit)
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    q.prepare("SELECT id, battery_serial, started_at, ended_at, duration_seconds "
              "FROM flight_sessions WHERE device_uid = ? ORDER BY id DESC LIMIT ?");
    q.addBindValue(deviceUid);
    q.addBindValue(limit);
    if (!q.exec()) return QStringLiteral("[]");

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["id"] = q.value(0).toInt();
        o["batterySerial"] = q.value(1).toString();
        o["startedAt"] = q.value(2).toString();
        o["endedAt"] = q.value(3).toString();
        o["durationSeconds"] = q.value(4).toDouble();
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

/**
 * @brief Compute a calibrated power model from recent flight session data.
 *
 * Averages Wh/km across the last N completed sessions that have both
 * energy and distance data.  Used for range prediction in the preflight
 * UI.  The model is only marked as "calibrated" if at least minSessions
 * data points are available.
 */
DatabaseManager::CalibratedPowerModel DatabaseManager::getCalibratedPowerModel(
    const QString &deviceUid, int minSessions)
{
    CalibratedPowerModel result;
    if (!m_initialized || deviceUid.isEmpty()) return result;

    QSqlQuery q(m_db);
    q.prepare("SELECT energy_consumed_wh, distance_m FROM flight_sessions "
              "WHERE device_uid = ? AND ended_at IS NOT NULL AND distance_m > 0 "
              "ORDER BY id DESC LIMIT ?");
    q.addBindValue(deviceUid);
    q.addBindValue(qMax(minSessions, 1));

    QVector<double> whPerKmValues;
    int hoverCount = 0;
    double hoverEnergySum = 0.0;

    if (!q.exec()) return result;

    while (q.next()) {
        double energyWh = q.value(0).toDouble();
        double distM = q.value(1).toDouble();
        if (energyWh > 0.0 && distM > 0.0) {
            whPerKmValues.append(energyWh / (distM / 1000.0));
            result.dataPointCount++;
        }
    }

    if (result.dataPointCount >= minSessions) {
        double sum = 0.0;
        for (double v : whPerKmValues) sum += v;
        result.whPerKm = sum / whPerKmValues.size();
        result.isCalibrated = true;
        qDebug() << "DatabaseManager: calibrated power model for" << deviceUid
                 << "whPerKm:" << result.whPerKm
                 << "from" << result.dataPointCount << "sessions";
    }

    return result;
}

QString DatabaseManager::getCheckConfig(const QString &checkId, const QString &key, int vehicleId)
{
    if (!m_initialized) return {};
    QSqlQuery q(m_db);
    if (vehicleId >= 0) {
        q.prepare("SELECT value FROM check_config WHERE check_id = ? AND key = ? AND vehicle_id = ?");
        q.addBindValue(checkId);
        q.addBindValue(key);
        q.addBindValue(vehicleId);
    } else {
        q.prepare("SELECT value FROM check_config WHERE check_id = ? AND key = ? AND vehicle_id IS NULL");
        q.addBindValue(checkId);
        q.addBindValue(key);
    }
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool DatabaseManager::setCheckConfig(const QString &checkId, const QString &key,
                                     const QString &value, int vehicleId)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    if (vehicleId >= 0) {
        q.prepare("INSERT OR REPLACE INTO check_config (vehicle_id, check_id, key, value, updated_at) "
                  "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)");
        q.addBindValue(vehicleId);
    } else {
        q.prepare("INSERT OR REPLACE INTO check_config (vehicle_id, check_id, key, value, updated_at) "
                  "VALUES (NULL, ?, ?, ?, CURRENT_TIMESTAMP)");
    }
    q.addBindValue(checkId);
    q.addBindValue(key);
    q.addBindValue(value);
    return execOrWarn(q, "setCheckConfig");
}

/**
 * @brief Build a composite vehicle history summary.
 *
 * Combines the vehicle profile, recent flight sessions, and battery health
 * trend into a single JSON object.  Used by the vehicle detail view and
 * the HTML export report.
 */
QString DatabaseManager::getVehicleHistory(const QString &deviceUid)
{
    // Returns a summary JSON: vehicle profile + recent sessions + battery health
    QString vehicle = getVehicle(deviceUid);
    if (vehicle.isEmpty()) return {};

    QJsonObject vehicleObj = QJsonDocument::fromJson(vehicle.toUtf8()).object();

    // Add recent sessions
    QString sessionsJson = getFlightSessions(deviceUid, 5);
    vehicleObj["recentSessions"] = QJsonDocument::fromJson(sessionsJson.toUtf8()).array();

    // Add battery health if last session has a battery
    QJsonArray sessions = vehicleObj["recentSessions"].toArray();
    if (!sessions.isEmpty()) {
        QString batSerial = sessions[0].toObject()["batterySerial"].toString();
        if (!batSerial.isEmpty()) {
            QString trend = getBatteryHealthTrend(batSerial);
            if (!trend.isEmpty())
                vehicleObj["batteryHealthTrend"] = QJsonDocument::fromJson(trend.toUtf8()).object();
            QString battery = getBattery(batSerial);
            if (!battery.isEmpty())
                vehicleObj["lastBattery"] = QJsonDocument::fromJson(battery.toUtf8()).object();
        }
    }

    return QString::fromUtf8(QJsonDocument(vehicleObj).toJson(QJsonDocument::Compact));
}

// ── Check result audit ──────────────────────────────────────────────────────
// Each individual checklist item evaluation is recorded here with its
// outcome (Passed, Failed, Warning, Skipped) for compliance reporting.

bool DatabaseManager::saveCheckResult(const QString &deviceUid, int flightSessionId,
                                     const QString &checkId, const QString &status,
                                     const QString &message)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO check_results (device_uid, flight_session_id, check_id, status, message, evaluated_at) "
              "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
    q.addBindValue(deviceUid);
    q.addBindValue(flightSessionId);
    q.addBindValue(checkId);
    q.addBindValue(status);
    q.addBindValue(message);
    return execOrWarn(q, "saveCheckResult");
}

QString DatabaseManager::getCheckResults(int flightSessionId)
{
    if (!m_initialized) return QStringLiteral("[]");
    QSqlQuery q(m_db);
    q.prepare("SELECT check_id, status, message, evaluated_at "
              "FROM check_results WHERE flight_session_id = ? ORDER BY evaluated_at ASC");
    q.addBindValue(flightSessionId);
    if (!q.exec()) return QStringLiteral("[]");

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["checkId"] = q.value(0).toString();
        o["status"] = q.value(1).toString();
        o["message"] = q.value(2).toString();
        o["evaluatedAt"] = q.value(3).toString();
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ── No-fly zone CRUD ────────────────────────────────────────────────────────

/// Inserts a new restricted-airspace zone.  Returns the new row id (or -1 on error).
int DatabaseManager::insertZone(const QString &name, const QString &description,
                                double lat, double lon, double radiusM,
                                const QString &reason, int createdBy)
{
    if (!m_initialized || name.trimmed().isEmpty()) return -1;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO no_fly_zones "
              "(name, description, latitude, longitude, radius_m, reason, created_by, created_at, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(name.trimmed());
    q.addBindValue(description);
    q.addBindValue(lat);
    q.addBindValue(lon);
    q.addBindValue(radiusM);
    q.addBindValue(reason.isEmpty() ? QStringLiteral("Regulatory") : reason);
    if (createdBy > 0) q.addBindValue(createdBy); else q.addBindValue(QVariant(QMetaType::fromType<int>()));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: insertZone failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool DatabaseManager::updateZone(int id, const QString &name, const QString &description,
                                 double lat, double lon, double radiusM,
                                 const QString &reason, bool active)
{
    if (!m_initialized || id <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE no_fly_zones SET "
              "name = ?, description = ?, latitude = ?, longitude = ?, radius_m = ?, "
              "reason = ?, active = ?, updated_at = ? WHERE id = ?");
    q.addBindValue(name.trimmed());
    q.addBindValue(description);
    q.addBindValue(lat);
    q.addBindValue(lon);
    q.addBindValue(radiusM);
    q.addBindValue(reason.isEmpty() ? QStringLiteral("Regulatory") : reason);
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(id);
    return execOrWarn(q, "updateZone");
}

bool DatabaseManager::deleteZone(int id)
{
    if (!m_initialized || id <= 0) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM no_fly_zones WHERE id = ?");
    q.addBindValue(id);
    return execOrWarn(q, "deleteZone");
}

QList<QVariantMap> DatabaseManager::getAllZones()
{
    QList<QVariantMap> zones;
    if (!m_initialized) return zones;
    QSqlQuery q(m_db);
    q.prepare("SELECT nz.id, nz.name, nz.description, nz.latitude, nz.longitude, nz.radius_m, "
              "nz.reason, nz.active, nz.created_at, nz.updated_at, "
              "nz.created_by, op.name AS creator_name "
              "FROM no_fly_zones nz LEFT JOIN operators op ON op.id = nz.created_by "
              "ORDER BY nz.id DESC");
    if (!q.exec()) return zones;
    while (q.next()) {
        QVariantMap z;
        z[QStringLiteral("id")] = q.value(0).toInt();
        z[QStringLiteral("name")] = q.value(1).toString();
        z[QStringLiteral("description")] = q.value(2).toString();
        z[QStringLiteral("latitude")] = q.value(3).toDouble();
        z[QStringLiteral("longitude")] = q.value(4).toDouble();
        z[QStringLiteral("radius_m")] = q.value(5).toDouble();
        z[QStringLiteral("reason")] = q.value(6).toString();
        z[QStringLiteral("active")] = q.value(7).toInt() != 0;
        z[QStringLiteral("created_at")] = q.value(8).toString();
        z[QStringLiteral("updated_at")] = q.value(9).toString();
        z[QStringLiteral("created_by")] = q.value(10).toInt();
        z[QStringLiteral("created_by_name")] = q.value(11).toString();
        zones.append(z);
    }
    return zones;
}

QList<QVariantMap> DatabaseManager::getActiveZones()
{
    QList<QVariantMap> zones;
    if (!m_initialized) return zones;
    QSqlQuery q(m_db);
    q.prepare("SELECT nz.id, nz.name, nz.description, nz.latitude, nz.longitude, nz.radius_m, "
              "nz.reason, nz.active, nz.created_at, nz.updated_at, "
              "nz.created_by, op.name AS creator_name "
              "FROM no_fly_zones nz LEFT JOIN operators op ON op.id = nz.created_by "
              "WHERE nz.active = 1 ORDER BY nz.id DESC");
    if (!q.exec()) return zones;
    while (q.next()) {
        QVariantMap z;
        z[QStringLiteral("id")] = q.value(0).toInt();
        z[QStringLiteral("name")] = q.value(1).toString();
        z[QStringLiteral("description")] = q.value(2).toString();
        z[QStringLiteral("latitude")] = q.value(3).toDouble();
        z[QStringLiteral("longitude")] = q.value(4).toDouble();
        z[QStringLiteral("radius_m")] = q.value(5).toDouble();
        z[QStringLiteral("reason")] = q.value(6).toString();
        z[QStringLiteral("active")] = q.value(7).toInt() != 0;
        z[QStringLiteral("created_at")] = q.value(8).toString();
        z[QStringLiteral("updated_at")] = q.value(9).toString();
        z[QStringLiteral("created_by")] = q.value(10).toInt();
        z[QStringLiteral("created_by_name")] = q.value(11).toString();
        zones.append(z);
    }
    return zones;
}

// ── Manual zone compliance logging ──────────────────────────────────────────

/// Records a manual airspace compliance check for a flight (audit trail).
bool DatabaseManager::insertComplianceRecord(int flightId, int operatorId,
                                             const QString &result,
                                             const QString &notes,
                                             const QString &overrideReason)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO zone_compliance_log "
              "(flight_id, operator_id, checked_at, result, notes, override_reason) "
              "VALUES (?, ?, ?, ?, ?, ?)");
    if (flightId > 0) q.addBindValue(flightId); else q.addBindValue(QVariant(QMetaType::fromType<int>()));
    if (operatorId > 0) q.addBindValue(operatorId); else q.addBindValue(QVariant(QMetaType::fromType<int>()));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(result);
    q.addBindValue(notes);
    q.addBindValue(overrideReason);
    return execOrWarn(q, "insertComplianceRecord");
}

QList<QVariantMap> DatabaseManager::getComplianceForFlight(int flightId)
{
    QList<QVariantMap> records;
    if (!m_initialized || flightId <= 0) return records;
    QSqlQuery q(m_db);
    q.prepare("SELECT zcl.id, zcl.flight_id, zcl.operator_id, zcl.checked_at, "
              "zcl.result, zcl.notes, zcl.override_reason, op.name AS operator_name "
              "FROM zone_compliance_log zcl LEFT JOIN operators op ON op.id = zcl.operator_id "
              "WHERE zcl.flight_id = ? ORDER BY zcl.checked_at DESC");
    q.addBindValue(flightId);
    if (!q.exec()) return records;
    while (q.next()) {
        QVariantMap r;
        r[QStringLiteral("id")] = q.value(0).toInt();
        r[QStringLiteral("flight_id")] = q.value(1).toInt();
        r[QStringLiteral("operator_id")] = q.value(2).toInt();
        r[QStringLiteral("checked_at")] = q.value(3).toString();
        r[QStringLiteral("result")] = q.value(4).toString();
        r[QStringLiteral("notes")] = q.value(5).toString();
        r[QStringLiteral("override_reason")] = q.value(6).toString();
        r[QStringLiteral("operator_name")] = q.value(7).toString();
        records.append(r);
    }
    return records;
}

QList<QVariantMap> DatabaseManager::getComplianceHistory(int limit)
{
    QList<QVariantMap> records;
    if (!m_initialized) return records;
    QSqlQuery q(m_db);
    q.prepare("SELECT zcl.id, zcl.flight_id, zcl.operator_id, zcl.checked_at, "
              "zcl.result, zcl.notes, zcl.override_reason, op.name AS operator_name "
              "FROM zone_compliance_log zcl LEFT JOIN operators op ON op.id = zcl.operator_id "
              "ORDER BY zcl.checked_at DESC LIMIT ?");
    q.addBindValue(limit);
    if (!q.exec()) return records;
    while (q.next()) {
        QVariantMap r;
        r[QStringLiteral("id")] = q.value(0).toInt();
        r[QStringLiteral("flight_id")] = q.value(1).toInt();
        r[QStringLiteral("operator_id")] = q.value(2).toInt();
        r[QStringLiteral("checked_at")] = q.value(3).toString();
        r[QStringLiteral("result")] = q.value(4).toString();
        r[QStringLiteral("notes")] = q.value(5).toString();
        r[QStringLiteral("override_reason")] = q.value(6).toString();
        r[QStringLiteral("operator_name")] = q.value(7).toString();
        records.append(r);
    }
    return records;
}
