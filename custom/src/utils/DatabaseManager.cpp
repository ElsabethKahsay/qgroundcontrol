// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/DatabaseManager.cpp
// Description: Implementation of SQLite storage for the pre-flight checklist.

#include "DatabaseManager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
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
bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    // checklist_templates table
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

    // ── Indexes ──────────────────────────────────────────────────────
    query.exec("CREATE INDEX IF NOT EXISTS idx_hardware_flight ON hardware_test_events(flight_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_hardware_timestamp ON hardware_test_events(timestamp)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_vehicle ON compliance_logs(vehicle_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_compliance_date ON compliance_logs(created_at)");

    // maintenance_components table
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

    // ── Phase 7: payload_weight_kg column ──────────────────────────
    // Use ALTER TABLE ADD COLUMN with IF NOT EXISTS for idempotency
    // SQLite doesn't support IF NOT EXISTS for ALTER TABLE, so check column exists first
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA table_info(flight_sessions)");
    bool hasPayload = false;
    while (pragma.next()) {
        if (pragma.value(1).toString() == QStringLiteral("payload_weight_kg"))
            hasPayload = true;
    }
    if (!hasPayload) {
        query.exec("ALTER TABLE flight_sessions ADD COLUMN payload_weight_kg REAL NOT NULL DEFAULT 0.0");
    }

    // ── Phase 7 indexes ──────────────────────────────────────────────
    query.exec("CREATE INDEX IF NOT EXISTS idx_battery_cycles_serial ON battery_cycles(battery_serial)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_battery_cycles_session ON battery_cycles(flight_session_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_flight_sessions_vehicle ON flight_sessions(device_uid)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_check_results_session ON check_results(flight_session_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_check_results_vehicle ON check_results(device_uid)");

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
 * @brief Query hardware test events for a flight
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
 * @brief Returns the current database schema version
 * @return Schema version number
 * 
 * Used for future migration support. Currently returns 1.
 */
bool DatabaseManager::execOrWarn(QSqlQuery &query, const char *tableName)
{
    if (!query.exec()) {
        qWarning() << "DatabaseManager:" << tableName << "error:" << query.lastError().text();
        return false;
    }
    return true;
}

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

int DatabaseManager::schemaVersion() const
{
    return 3;
}

// ── Component maintenance ─────────────────────────────────────────────────

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

// ── Battery CRUD ────────────────────────────────────────────────────────────

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

bool DatabaseManager::endFlightSession(int sessionId, double durationSeconds)
{
    if (!m_initialized) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE flight_sessions SET ended_at = CURRENT_TIMESTAMP, duration_seconds = ? WHERE id = ?");
    q.addBindValue(durationSeconds);
    q.addBindValue(sessionId);
    return execOrWarn(q, "endFlightSession");
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
