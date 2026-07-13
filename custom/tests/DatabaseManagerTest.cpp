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
}

UT_REGISTER_TEST(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
