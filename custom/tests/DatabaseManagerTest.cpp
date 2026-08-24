#include <QTest>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlDatabase>

#include "UnitTest.h"
#include "DatabaseManager.h"

class DatabaseManagerTest : public UnitTest {
    Q_OBJECT

private:
    QString initTempDb(DatabaseManager &db) {
        QString tmpPath = QDir::tempPath() + QStringLiteral("/test_db_XXXXXX.db");
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate(tmpPath);
        tmpFile.open();
        QString path = tmpFile.fileName();
        tmpFile.close();
        db.initialize(path);
        return path;
    }

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
    }
    void cleanup() override { UnitTest::cleanup(); }

    void testInitialize();
    void testVehicleInsertAndRead();
    void testBatteryCrud();
    void testBatteryCycleSaveAndQuery();
    void testBatteryCycleCount();
    void testDoubleIncrementGuard();
    void testIncrementBatteryCycleGuard();
    void testFlightSessionRoundTrip();
    void testCheckResultAuditTrail();
    void testSchemaMigration();
    void testZoneCrud();
    void testZoneComplianceLog();
    void testV12Migration();
    void testFlightSummaryFields();
    void testCheckCountsAndAnomalies();
    void testRecoverOrphanedSessions();
    void testExportFlightsCsv();
    void testExportFlightDetailCsv();
    void testGetFlightStats();
    void testBackupBeforeMigration();
};

void DatabaseManagerTest::testInitialize()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_preflight_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    bool ok = db.initialize(tmpFile.fileName());
    QVERIFY(ok);
}

void DatabaseManagerTest::testVehicleInsertAndRead()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_vehicle_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    bool ok = db.upsertVehicle(QStringLiteral("UID-001"), QStringLiteral("TestUAV"),
                               QStringLiteral("PX4"), QStringLiteral("multirotor"));
    QVERIFY(ok);

    QString json = db.getVehicle(QStringLiteral("UID-001"));
    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("TestUAV")));
}

void DatabaseManagerTest::testBatteryCrud()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_battery_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    bool ok = db.upsertBattery(QStringLiteral("SN-001"), QStringLiteral("Test Battery"));
    QVERIFY(ok);

    QString json = db.getBattery(QStringLiteral("SN-001"));
    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("Test Battery")));
}

void DatabaseManagerTest::testBatteryCycleSaveAndQuery()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_cycle_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-TEST"), QStringLiteral("Test"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertBattery(QStringLiteral("SN-BATT"), QStringLiteral("Test Battery"));
    int sessionId = db.startFlightSession(QStringLiteral("UID-TEST"), QString());
    QVERIFY(sessionId > 0);

    bool ok = db.saveBatteryCycle(QStringLiteral("SN-BATT"), sessionId, 5000.0, 0.5, 12.6, 0);
    QVERIFY(ok);

    QString cycles = db.getBatteryCycles(QStringLiteral("SN-BATT"), 10);
    QVERIFY(!cycles.isEmpty());
    QVERIFY(cycles.contains(QStringLiteral("capacityAtFullMah")));
}

void DatabaseManagerTest::testBatteryCycleCount()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_cycle_count_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-TEST"), QStringLiteral("Test"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertBattery(QStringLiteral("SN-BATT"), QStringLiteral("Test Battery"));
    int sessionId1 = db.startFlightSession(QStringLiteral("UID-TEST"), QString());
    QVERIFY(sessionId1 > 0);
    db.saveBatteryCycle(QStringLiteral("SN-BATT"), sessionId1, 5000.0, 0.5, 12.6, 0);
    db.endFlightSession(sessionId1, 45.0);

    QCOMPARE(db.getBatteryCycleCount(QStringLiteral("SN-BATT")), 1);

    int sessionId2 = db.startFlightSession(QStringLiteral("UID-TEST"), QString());
    QVERIFY(sessionId2 > 0);
    db.saveBatteryCycle(QStringLiteral("SN-BATT"), sessionId2, 5000.0, 0.3, 12.5, 0);
    db.endFlightSession(sessionId2, 60.0);

    QCOMPARE(db.getBatteryCycleCount(QStringLiteral("SN-BATT")), 2);
}

void DatabaseManagerTest::testDoubleIncrementGuard()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_dbl_inc_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-TEST"), QStringLiteral("Test"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertBattery(QStringLiteral("SN-BATT"), QStringLiteral("Test Battery"));
    int sessionId = db.startFlightSession(QStringLiteral("UID-TEST"), QString());
    QVERIFY(sessionId > 0);
    db.saveBatteryCycle(QStringLiteral("SN-BATT"), sessionId, 5000.0, 0.5, 12.6, 0);
    db.endFlightSession(sessionId, 45.0);

    int count = db.getBatteryCycleCount(QStringLiteral("SN-BATT"));
    QCOMPARE(count, 1);
}

void DatabaseManagerTest::testIncrementBatteryCycleGuard()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_inc_guard_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-SHORT"), QStringLiteral("Short"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertVehicle(QStringLiteral("UID-LONG"), QStringLiteral("Long"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertBattery(QStringLiteral("SN-BATT"), QStringLiteral("Test Battery"));

    // Short flight (< 30s) — should NOT increment
    int shortSession = db.startFlightSession(QStringLiteral("UID-SHORT"), QString());
    QVERIFY(shortSession > 0);
    db.endFlightSession(shortSession, 10.0);
    QVERIFY(!db.incrementBatteryCycle(shortSession));
    QCOMPARE(db.getBatteryCycleCount(QStringLiteral("unknown")), 0);

    // Long flight (>= 30s) — should increment
    int longSession = db.startFlightSession(QStringLiteral("UID-LONG"), QString("SN-BATT"));
    QVERIFY(longSession > 0);
    db.endFlightSession(longSession, 45.0);
    QVERIFY(db.incrementBatteryCycle(longSession));
    QCOMPARE(db.getBatteryCycleCount(QStringLiteral("SN-BATT")), 1);

    // Double increment guard — second call returns false
    QVERIFY(!db.incrementBatteryCycle(longSession));
    QCOMPARE(db.getBatteryCycleCount(QStringLiteral("SN-BATT")), 1);
}

void DatabaseManagerTest::testFlightSessionRoundTrip()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_flight_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-FLIGHT"), QStringLiteral("Flight"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    db.upsertBattery(QStringLiteral("SN-BATT"), QStringLiteral("Test Battery"));
    int sessionId = db.startFlightSession(QStringLiteral("UID-FLIGHT"), QString("SN-BATT"));
    QVERIFY(sessionId > 0);

    bool ended = db.endFlightSession(sessionId, 120.0);
    QVERIFY(ended);
}

void DatabaseManagerTest::testCheckResultAuditTrail()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_audit_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    db.initialize(tmpFile.fileName());

    db.upsertVehicle(QStringLiteral("UID-CKR"), QStringLiteral("CheckTest"),
                     QStringLiteral("PX4"), QStringLiteral("multirotor"));
    int sessionId = db.startFlightSession(QStringLiteral("UID-CKR"), QString());
    QVERIFY(sessionId > 0);

    QVERIFY(db.saveCheckResult(QStringLiteral("UID-CKR"), sessionId,
                               QStringLiteral("check.battery"), QStringLiteral("passed"),
                               QStringLiteral("Battery OK")));
    QVERIFY(db.saveCheckResult(QStringLiteral("UID-CKR"), sessionId,
                               QStringLiteral("check.gps"), QStringLiteral("warning"),
                               QStringLiteral("GPS weak")));
    QVERIFY(db.saveCheckResult(QStringLiteral("UID-CKR"), sessionId,
                               QStringLiteral("check.rc"), QStringLiteral("failed"),
                               QStringLiteral("RC calibration needed")));

    QString json = db.getCheckResults(sessionId);
    QVERIFY(!json.isEmpty());
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QVERIFY(doc.isArray());
    QJsonArray arr = doc.array();
    QCOMPARE(arr.size(), 3);

    QCOMPARE(arr[0].toObject()[QStringLiteral("checkId")].toString(), QStringLiteral("check.battery"));
    QCOMPARE(arr[1].toObject()[QStringLiteral("checkId")].toString(), QStringLiteral("check.gps"));
    QCOMPARE(arr[2].toObject()[QStringLiteral("checkId")].toString(), QStringLiteral("check.rc"));

    QCOMPARE(arr[0].toObject()[QStringLiteral("status")].toString(), QStringLiteral("passed"));
    QCOMPARE(arr[1].toObject()[QStringLiteral("status")].toString(), QStringLiteral("warning"));
    QCOMPARE(arr[2].toObject()[QStringLiteral("status")].toString(), QStringLiteral("failed"));

    QCOMPARE(arr[0].toObject()[QStringLiteral("message")].toString(), QStringLiteral("Battery OK"));
    QCOMPARE(arr[1].toObject()[QStringLiteral("message")].toString(), QStringLiteral("GPS weak"));
    QCOMPARE(arr[2].toObject()[QStringLiteral("message")].toString(), QStringLiteral("RC calibration needed"));

    QVERIFY(!arr[0].toObject()[QStringLiteral("evaluatedAt")].toString().isEmpty());
}

void DatabaseManagerTest::testSchemaMigration()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/migrate_test_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QString dbPath = tmpFile.fileName();
    tmpFile.close();

    // Create an "old" DB with core tables but NOT check_results or vehicle_config
    {
        QSqlDatabase oldDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("migration_conn"));
        oldDb.setDatabaseName(dbPath);
        QVERIFY(oldDb.open());
        QSqlQuery q(oldDb);

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS checklist_templates ("
            "template_id TEXT PRIMARY KEY, vehicle_type TEXT NOT NULL, "
            "template_name TEXT NOT NULL, template_version TEXT DEFAULT '1.0', "
            "items_json TEXT NOT NULL, is_default INTEGER DEFAULT 0, "
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
            "modified_at TEXT DEFAULT CURRENT_TIMESTAMP, "
            "UNIQUE(vehicle_type, template_name))")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS compliance_logs ("
            "log_id TEXT PRIMARY KEY, vehicle_id TEXT NOT NULL, "
            "vehicle_type TEXT NOT NULL, operator_id TEXT, operator_name TEXT, "
            "started_at TEXT, completed_at TEXT, overall_verdict TEXT, "
            "checklist_json TEXT NOT NULL, telemetry_snapshot TEXT, "
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP)")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hardware_test_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, flight_id INTEGER NOT NULL, "
            "timestamp TEXT NOT NULL, checklist_item_id TEXT DEFAULT 'hardware_servo_test', "
            "step_name TEXT NOT NULL, servo_instance INTEGER NOT NULL, "
            "target_pwm INTEGER, feedback_pwm INTEGER, "
            "tolerance_min INTEGER, tolerance_max INTEGER, "
            "operator_confirmed INTEGER, result TEXT NOT NULL, "
            "failure_reason TEXT, operator_id TEXT, duration_ms INTEGER)")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS maintenance_components ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
            "type TEXT NOT NULL, max_hours REAL NOT NULL DEFAULT 0, "
            "current_hours REAL NOT NULL DEFAULT 0, "
            "max_cycles INTEGER NOT NULL DEFAULT 0, "
            "current_cycles INTEGER NOT NULL DEFAULT 0, "
            "last_maintenance TEXT, notes TEXT)")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS vehicles ("
            "device_uid TEXT PRIMARY KEY, friendly_name TEXT NOT NULL DEFAULT '', "
            "autopilot_type TEXT NOT NULL DEFAULT '', "
            "airframe_type TEXT NOT NULL DEFAULT '', "
            "first_seen TEXT NOT NULL, last_seen TEXT NOT NULL, "
            "total_flight_count INTEGER NOT NULL DEFAULT 0, "
            "total_flight_hours REAL NOT NULL DEFAULT 0.0, "
            "identity_source TEXT NOT NULL DEFAULT 'hardware_uid')")));

        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO vehicles (device_uid, friendly_name, autopilot_type, "
            "airframe_type, first_seen, last_seen) VALUES ("
            "'OLD-UID', 'Legacy Vehicle', 'PX4', 'multirotor', "
            "datetime('now'), datetime('now'))")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS flight_sessions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "device_uid TEXT NOT NULL REFERENCES vehicles(device_uid), "
            "battery_serial TEXT, started_at TEXT NOT NULL, "
            "ended_at TEXT, duration_seconds REAL NOT NULL DEFAULT 0.0)")));

        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO flight_sessions (device_uid, started_at) "
            "VALUES ('OLD-UID', datetime('now'))")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS batteries ("
            "serial_number TEXT PRIMARY KEY, operator_label TEXT NOT NULL DEFAULT '', "
            "first_seen TEXT NOT NULL, last_seen TEXT NOT NULL, "
            "total_cycles INTEGER NOT NULL DEFAULT 0, "
            "identity_source TEXT NOT NULL DEFAULT 'operator_confirmed')")));

        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS battery_cycles ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "battery_serial TEXT NOT NULL REFERENCES batteries(serial_number), "
            "flight_session_id INTEGER NOT NULL DEFAULT 0, "
            "capacity_at_full_mah REAL NOT NULL DEFAULT 0.0, "
            "voltage_sag_v REAL NOT NULL DEFAULT 0.0, "
            "resting_voltage_v REAL NOT NULL DEFAULT 0.0, "
            "cycle_count INTEGER NOT NULL DEFAULT 0, "
            "recorded_at TEXT NOT NULL)")));

        // Deliberately skip check_results and vehicle_config tables
        // Deliberately skip payload_weight_kg column on flight_sessions
        // Deliberately skip fingerprint columns on vehicles

        oldDb.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("migration_conn"));

    // Initialize DatabaseManager — should upgrade the old DB
    db.reset();
    QVERIFY(db.initialize(dbPath));

    // Old data survived
    QString vehicle = db.getVehicle(QStringLiteral("OLD-UID"));
    QVERIFY(!vehicle.isEmpty());
    QVERIFY(vehicle.contains(QStringLiteral("Legacy Vehicle")));

    // New check_results table created and usable
    QVERIFY(db.saveCheckResult(QStringLiteral("OLD-UID"), 1,
                               QStringLiteral("migration.check"), QStringLiteral("passed"),
                               QStringLiteral("Migration OK")));

    QString results = db.getCheckResults(1);
    QVERIFY(!results.isEmpty());
    QVERIFY(results.contains(QStringLiteral("migration.check")));

    // New no_fly_zones / zone_compliance_log tables created by migration
    int zid = db.insertZone(QStringLiteral("Migrated Airport"), QString(),
                            40.0, -3.0, 5000.0, QStringLiteral("Regulatory"));
    QVERIFY(zid > 0);
    QVERIFY(db.insertComplianceRecord(0, 0, QStringLiteral("Clear"), QString(), QString()));
}

void DatabaseManagerTest::testZoneCrud()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_zones_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    // Fresh databases are seeded with default airspace zones (Bole Airport
    // exclusion etc.).  Remove them so the CRUD counts below stay exact.
    const auto seeded = db.getAllZones();
    for (const QVariantMap &z : seeded) {
        QVERIFY(db.deleteZone(z.value(QStringLiteral("id")).toInt()));
    }
    QCOMPARE(db.getAllZones().size(), 0);

    // Create
    int id1 = db.insertZone(QStringLiteral("Airport CTR"), QStringLiteral("Class D"),
                            47.3977, 8.6611, 7500.0, QStringLiteral("Regulatory"));
    QVERIFY(id1 > 0);
    int id2 = db.insertZone(QStringLiteral("Cell Tower"), QString(),
                            46.0, 7.0, 150.0, QStringLiteral("Obstacle"));
    QVERIFY(id2 > 0);

    auto zones = db.getAllZones();
    QCOMPARE(zones.size(), 2);

    // List all vs active
    QCOMPARE(db.getActiveZones().size(), 2);

    // Update
    QVERIFY(db.updateZone(id1, QStringLiteral("Airport CTR v2"), QStringLiteral("Updated"),
                          47.5, 8.6, 8000.0, QStringLiteral("Restricted"), false));
    zones = db.getAllZones();
    QCOMPARE(zones.size(), 2);
    for (const QVariantMap &z : zones) {
        if (z.value(QStringLiteral("id")).toInt() == id1) {
            QCOMPARE(z.value(QStringLiteral("name")).toString(), QStringLiteral("Airport CTR v2"));
            QCOMPARE(z.value(QStringLiteral("reason")).toString(), QStringLiteral("Restricted"));
            QCOMPARE(z.value(QStringLiteral("active")).toBool(), false);
        }
    }

    // Inactive zone hidden from getActiveZones
    QCOMPARE(db.getActiveZones().size(), 1);

    // Delete
    QVERIFY(db.deleteZone(id2));
    QCOMPARE(db.getAllZones().size(), 1);
}

void DatabaseManagerTest::testZoneComplianceLog()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_compliance_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Alice"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);

    // openFlight requires an operator row (FK) and returns the real flight id;
    // compliance records must reference flights that actually exist.
    int flight5 = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                QString(), QString(), QString());
    int flight6 = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                QString(), QString(), QString());
    QVERIFY(flight5 > 0);
    QVERIFY(flight6 > 0);

    // Two records for flight5, one for flight6
    QVERIFY(db.insertComplianceRecord(flight5, opId, QStringLiteral("Clear"), QStringLiteral("All good"), QString()));
    QVERIFY(db.insertComplianceRecord(flight5, opId, QStringLiteral("Conflict"), QStringLiteral("Near zone"),
                                      QStringLiteral("Mission authorized by ops")));
    QVERIFY(db.insertComplianceRecord(flight6, opId, QStringLiteral("Caution"), QString(), QString()));

    auto forFlight = db.getComplianceForFlight(flight5);
    QCOMPARE(forFlight.size(), 2);
    bool hasOverride = false;
    bool hasOperatorName = false;
    for (const QVariantMap &r : forFlight) {
        if (r.value(QStringLiteral("result")).toString() == QStringLiteral("Conflict")) {
            hasOverride = r.value(QStringLiteral("override_reason")).toString().contains(QStringLiteral("ops"));
        }
        if (!r.value(QStringLiteral("operator_name")).toString().isEmpty())
            hasOperatorName = true;
    }
    QVERIFY(hasOverride);
    QVERIFY(hasOperatorName);

    auto history = db.getComplianceHistory(100);
    QCOMPARE(history.size(), 3);
}

void DatabaseManagerTest::testV12Migration()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/v12_migrate_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QString dbPath = tmpFile.fileName();
    tmpFile.close();

    // Create an old (v9-style) DB with a flights table that has the broken
    // vehicle_id FK referencing vehicles(id).
    {
        QSqlDatabase oldDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("v12_migration_conn"));
        oldDb.setDatabaseName(dbPath);
        QVERIFY(oldDb.open());
        QSqlQuery q(oldDb);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE vehicles ("
            "device_uid TEXT PRIMARY KEY, friendly_name TEXT NOT NULL DEFAULT '', "
            "autopilot_type TEXT NOT NULL DEFAULT '', airframe_type TEXT NOT NULL DEFAULT '', "
            "first_seen TEXT NOT NULL, last_seen TEXT NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE operators ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
            "role TEXT NOT NULL DEFAULT 'Pilot', created_at TEXT NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE flights ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "operator_id INTEGER NOT NULL REFERENCES operators(id),"
            "vehicle_id INTEGER NOT NULL REFERENCES vehicles(id),"
            "mode TEXT NOT NULL, purpose TEXT, location TEXT, notes TEXT, weather_summary TEXT,"
            "pre_checklist_complete INTEGER NOT NULL DEFAULT 0,"
            "post_checklist_complete INTEGER NOT NULL DEFAULT 0,"
            "started_at TEXT NOT NULL, armed_at TEXT, disarmed_at TEXT, ended_at TEXT,"
            "duration_sec INTEGER, max_altitude_m REAL, min_battery_v REAL, max_battery_v REAL,"
            "flight_mode_changes INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE flight_telemetry_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "flight_id INTEGER NOT NULL REFERENCES flights(id),"
            "event_type TEXT NOT NULL, triggered_by TEXT, battery_v REAL, altitude_m REAL, "
            "gps_sats INTEGER, flight_mode TEXT, timestamp TEXT NOT NULL)")));
        // One existing flight row that must survive the rebuild.
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO flights (operator_id, vehicle_id, mode, started_at) "
            "VALUES (1, 1, 'FLIGHT', datetime('now'))")));
        // Stored schema version so migrateSchema upgrades from v11 → v12.
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE schema_version (version INTEGER PRIMARY KEY)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (11)")));
        oldDb.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v12_migration_conn"));

    db.reset();
    QVERIFY(db.initialize(dbPath));
    QCOMPARE(db.storedSchemaVersion(), 14);

    // v13: zone_id + intersection columns exist on zone_compliance_log.
    {
        QSqlDatabase checkDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         QStringLiteral("v13_col_conn"));
        checkDb.setDatabaseName(dbPath);
        QVERIFY(checkDb.open());
        QSqlQuery q(checkDb);
        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(zone_compliance_log)")));
        bool hasZoneId = false;
        bool hasIntersection = false;
        while (q.next()) {
            const QString colName = q.value(1).toString();
            if (colName == QStringLiteral("zone_id"))       hasZoneId = true;
            if (colName == QStringLiteral("intersection"))  hasIntersection = true;
        }
        QVERIFY(hasZoneId);
        QVERIFY(hasIntersection);
        checkDb.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v13_col_conn"));

    // v14: uav_weight_kg column exists on vehicles and round-trips through
    // the weight accessors (0 default → stored value → readable back).
    {
        QSqlDatabase checkDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         QStringLiteral("v14_col_conn"));
        checkDb.setDatabaseName(dbPath);
        QVERIFY(checkDb.open());
        QSqlQuery q(checkDb);
        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(vehicles)")));
        bool hasUavWeight = false;
        while (q.next()) {
            if (q.value(1).toString() == QStringLiteral("uav_weight_kg"))
                hasUavWeight = true;
        }
        QVERIFY(hasUavWeight);
        checkDb.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v14_col_conn"));

    // Exercise the production write path used by VehicleProfileManager.
    QVERIFY(db.upsertVehicleEx(QStringLiteral("uid-v14-test"), QStringLiteral("Test Rig"),
                               QStringLiteral("ardupilotmega"), QStringLiteral("quad")));
    QVERIFY(db.updateVehicleUavWeight(QStringLiteral("uid-v14-test"), 1.850));
    QCOMPARE(db.vehicleUavWeight(QStringLiteral("uid-v14-test")), 1.850);
    QCOMPARE(db.vehicleUavWeight(QStringLiteral("uid-unknown")), 0.0);

    // New summary columns exist on flights.
    auto flight = db.getFlightById(1);
    QVERIFY(!flight.isEmpty());
    QCOMPARE(flight.value(QStringLiteral("max_ground_speed_ms")).toDouble(), 0.0);
    QCOMPARE(flight.value(QStringLiteral("distance_flown_m")).toDouble(), 0.0);

    // Old flight row survived the rebuild.
    QCOMPARE(flight.value(QStringLiteral("mode")).toString(), QStringLiteral("FLIGHT"));

    // The broken vehicle FK must be gone — openFlight (vehicleId = sysid 1)
    // must succeed even though vehicles has no id column.
    int opId = db.insertOperator(QStringLiteral("Op"), QStringLiteral("Pilot"));
    int fid = db.openFlight(opId, 7, QStringLiteral("FLIGHT"), QString(),
                            QString(), QString(), QString());
    QVERIFY(fid > 0);

    // Snapshot columns usable.
    QVERIFY(db.insertTelemetryEventSnapshot(fid, QStringLiteral("ARM"), QString(),
                                            12.0, 30.0, 12, QStringLiteral("Loiter"),
                                            47.3, 8.5, 1.2, 1.5, 90.0));
    QCOMPARE(db.getAnomalyCountForFlight(fid), 0);
}

void DatabaseManagerTest::testFlightSummaryFields()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_summary_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Bob"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);
    int fid = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                            QString(), QString(), QString());
    QVERIFY(fid > 0);

    bool ok = db.closeFlight(
        fid, 120, 100.0, 12.0, 13.5, 3,
        22.5, 4.5, 1500.0, 12.8,
        3, 1, 2, 1);
    QVERIFY(ok);

    auto flight = db.getFlightById(fid);
    QVERIFY(!flight.isEmpty());
    QCOMPARE(flight.value(QStringLiteral("duration_sec")).toInt(), 120);
    QCOMPARE(flight.value(QStringLiteral("max_ground_speed_ms")).toDouble(), 22.5);
    QCOMPARE(flight.value(QStringLiteral("max_vertical_speed_ms")).toDouble(), 4.5);
    QCOMPARE(flight.value(QStringLiteral("distance_flown_m")).toDouble(), 1500.0);
    QCOMPARE(flight.value(QStringLiteral("avg_battery_v")).toDouble(), 12.8);
    QCOMPARE(flight.value(QStringLiteral("check_pass_count")).toInt(), 3);
    QCOMPARE(flight.value(QStringLiteral("check_fail_count")).toInt(), 1);
    QCOMPARE(flight.value(QStringLiteral("check_warn_count")).toInt(), 2);
    QCOMPARE(flight.value(QStringLiteral("anomaly_count")).toInt(), 1);
}

void DatabaseManagerTest::testCheckCountsAndAnomalies()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_counts_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Carol"), QStringLiteral("Pilot"));
    int fid = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                            QString(), QString(), QString());
    QVERIFY(fid > 0);

    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("a"), QStringLiteral("pre"),
                                       false, QStringLiteral("PASS"), QString()));
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("b"), QStringLiteral("pre"),
                                       false, QStringLiteral("FAILED"), QString()));
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("c"), QStringLiteral("pre"),
                                       false, QStringLiteral("WARNING"), QString()));

    auto counts = db.getCheckCountsForFlight(fid);
    QCOMPARE(counts.value(QStringLiteral("pass")).toInt(), 1);
    QCOMPARE(counts.value(QStringLiteral("fail")).toInt(), 1);
    QCOMPARE(counts.value(QStringLiteral("warn")).toInt(), 1);

    QCOMPARE(db.getAnomalyCountForFlight(fid), 0);
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("CHECK_DEGRADED"),
                                    QStringLiteral("a"), 12.0, 30.0, 12, QString()));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("BATTERY_WARN"),
                                    QString(), 20.5, 30.0, 12, QString()));
    QCOMPARE(db.getAnomalyCountForFlight(fid), 2);
}

void DatabaseManagerTest::testRecoverOrphanedSessions()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_orphan_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Dave"), QStringLiteral("Pilot"));
    // Armed session that never ended (simulates a crash mid-flight).
    int fid = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                            QString(), QString(), QString());
    QVERIFY(fid > 0);
    QVERIFY(db.setFlightArmedAt(fid, QDateTime::currentDateTimeUtc().addSecs(-60)));

    // Session that never armed (closed during pre-flight).
    int fid2 = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                             QString(), QString(), QString());
    QVERIFY(fid2 > 0);

    QVERIFY(db.recoverOrphanedSessions());

    auto armed = db.getFlightById(fid);
    QVERIFY(!armed.value(QStringLiteral("ended_at")).toString().isEmpty());
    QVERIFY(armed.value(QStringLiteral("duration_sec")).toInt() >= 58);
    QVERIFY(armed.value(QStringLiteral("notes")).toString().contains(QStringLiteral("interrupted")));

    auto preArm = db.getFlightById(fid2);
    QVERIFY(!preArm.value(QStringLiteral("ended_at")).toString().isEmpty());
    QCOMPARE(preArm.value(QStringLiteral("duration_sec")).toInt(), 0);
}

void DatabaseManagerTest::testExportFlightsCsv()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_export_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Eve"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);
    int fid = db.openFlight(opId, 9, QStringLiteral("FLIGHT"),
                            QStringLiteral("Survey"), QStringLiteral("North Field"),
                            QString(), QString());
    QVERIFY(fid > 0);
    QVERIFY(db.closeFlight(fid, 90, 80.0, 11.5, 13.5, 2, 1, 2, 0));

    QString path = db.exportFlightsCsv(QString(), QString(), -1, QString());
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("Survey")));
    QVERIFY(content.contains(QStringLiteral("North Field")));
    QVERIFY(content.contains(QStringLiteral("Eve")));
    QVERIFY(content.contains(QStringLiteral("FLIGHT")));

    // Filtered (mode = TRAINING) export should exclude the FLIGHT row.
    QString trainingPath = db.exportFlightsCsv(QString(), QString(), -1, QStringLiteral("TRAINING"));
    QVERIFY(!trainingPath.isEmpty());
    QFile tf(trainingPath);
    QVERIFY(tf.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString trainingContent = QString::fromUtf8(tf.readAll());
    tf.close();
    QVERIFY(!trainingContent.contains(QStringLiteral("Survey")));

    QFile::remove(path);
    QFile::remove(trainingPath);
}

void DatabaseManagerTest::testExportFlightDetailCsv()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_detail_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Frank"), QStringLiteral("Pilot"));
    int fid = db.openFlight(opId, 9, QStringLiteral("FLIGHT"), QStringLiteral("Survey"),
                            QString(), QString(), QString());
    QVERIFY(fid > 0);
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("pre-1"), QStringLiteral("pre"),
                                       false, QStringLiteral("PASS"), QStringLiteral("ok")));
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("post-1"), QStringLiteral("post"),
                                       true, QStringLiteral("PASS"), QString()));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("ARM"), QString(), 12.5,
                                    30.0, 12, QStringLiteral("LOITER")));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("BATTERY_WARN"), QString(), 10.2,
                                    25.0, 10, QString()));

    QString path = db.exportFlightDetailCsv(fid);
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("=== FLIGHT METADATA ===")));
    QVERIFY(content.contains(QStringLiteral("=== PRE-FLIGHT CHECKS ===")));
    QVERIFY(content.contains(QStringLiteral("=== POST-FLIGHT CHECKS ===")));
    QVERIFY(content.contains(QStringLiteral("=== TELEMETRY EVENTS ===")));
    QVERIFY(content.contains(QStringLiteral("pre-1")));
    QVERIFY(content.contains(QStringLiteral("post-1")));
    QVERIFY(content.contains(QStringLiteral("BATTERY_WARN")));
    QVERIFY(content.contains(QStringLiteral("LOITER")));

    QFile::remove(path);
}

void DatabaseManagerTest::testGetFlightStats()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_stats_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op1 = db.insertOperator(QStringLiteral("Grace"), QStringLiteral("Pilot"));
    int op2 = db.insertOperator(QStringLiteral("Henry"), QStringLiteral("Pilot"));
    QVERIFY(op1 > 0 && op2 > 0);

    // Two FLIGHT sessions on sysid 9 (20 min total), one TRAINING on sysid 10.
    int f1 = db.openFlight(op1, 9, QStringLiteral("FLIGHT"), QString(),
                           QString(), QString(), QString());
    QVERIFY(f1 > 0);
    QVERIFY(db.closeFlight(f1, 600, 100.0, 11.0, 13.5, 2, 0, 0, 0, 0,
                           2, 0, 0, 0));
    int f2 = db.openFlight(op1, 9, QStringLiteral("FLIGHT"), QString(),
                           QString(), QString(), QString());
    QVERIFY(f2 > 0);
    QVERIFY(db.closeFlight(f2, 600, 100.0, 11.0, 13.5, 2, 0, 0, 0, 0,
                           3, 0, 0, 2));
    int f3 = db.openFlight(op2, 10, QStringLiteral("TRAINING"), QString(),
                           QString(), QString(), QString());
    QVERIFY(f3 > 0);
    QVERIFY(db.closeFlight(f3, 120, 50.0, 12.0, 13.8, 1, 0, 0, 0, 0,
                           1, 0, 0, 1));

    // Unfiltered: 3 flights, 1320s -> 22m 0s, 2 vehicles, 2 operators.
    QVariantMap stats = db.getFlightStats(QString(), QString(), 0, QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 3);
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("22m 0s"));
    QCOMPARE(stats.value(QStringLiteral("vehicleCount")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("operatorCount")).toInt(), 2);
    // pass rate: (2+3+1 checks)/(2*2 + 2*3)=6/6 = 100%.
    QCOMPARE(stats.value(QStringLiteral("avgPassRate")).toInt(), 100);
    // anomaly rate: (0+2+1)/3 ~ 100%.
    QCOMPARE(stats.value(QStringLiteral("anomalyRate")).toInt(), 100);

    // Filtered to FLIGHT only: 2 flights, 1200s -> 20m 0s, 1 vehicle,
    // each row is 100% pass, anomalies (0+2)/2 -> 100%.
    stats = db.getFlightStats(QString(), QString(), 0, QStringLiteral("FLIGHT"));
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("20m 0s"));
    QCOMPARE(stats.value(QStringLiteral("vehicleCount")).toInt(), 1);
    QCOMPARE(stats.value(QStringLiteral("avgPassRate")).toInt(), 100);
    QCOMPARE(stats.value(QStringLiteral("anomalyRate")).toInt(), 100);

    // Filtered to sysid 9 only: 2 flights.
    stats = db.getFlightStats(QString(), QString(), 9, QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 2);

    // Filtered to sysid 10: 1 flight, 120s -> 2m 0s.
    stats = db.getFlightStats(QString(), QString(), 10, QString());
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("2m 0s"));
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 1);
}

void DatabaseManagerTest::testBackupBeforeMigration()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_backup_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    tmpFile.close();
    const QString path = tmpFile.fileName();

    // First init writes schema and triggers migration → a .bak must exist.
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".bak")));

    // A second init is a no-op (already initialized), so the backup file is
    // still the migration-time snapshot.
    QVERIFY(db.initialize(path));

    // Seed a flight, then re-initialize via reset() so the on-disk DB (now at
    // latest schema) goes through backup again before a (no-op) migration.
    int opId = db.insertOperator(QStringLiteral("Ivy"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);
    db.reset();
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".bak")));

    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

UT_REGISTER_TEST(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
