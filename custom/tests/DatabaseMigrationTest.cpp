/**
 * @file DatabaseMigrationTest.cpp
 * @brief Tests for DatabaseManager schema migration.
 *
 * Covers:
 *   - Fresh DB initializes to latest schema
 *   - Schema version is correct after init
 *   - All expected tables exist
 *   - Migration from stored version to latest works
 *   - Schema version tracking across upgrades
 */

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QFile>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QUuid>
#include <QTemporaryDir>
#include <QSqlError>

#include "UnitTest.h"
#include "utils/DatabaseManager.h"

class DatabaseMigrationTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
    }
    void cleanup() override { UnitTest::cleanup(); }

    void testFreshDBSchemaVersion();
    void testAllTablesExist();
    void testSchemaVersionConsistent();
    void testFlightSessionsTableStructure();
    void testComplianceLogsTableStructure();
    void testCheckConfigTableStructure();
    void testVehicleConfigTableStructure();
    void testNoFlyZonesTableExists();

    // Migrations run once, roll back on failure, and report why
    void testRestartDoesNotRerunMigrations();
    void testUpgradeFrom18AddsFlightTargetColumns();
    void testFailedStepRollsBackAndStops();
    void testUnopenablePathSetsStartupError();
    void testOneDatabaseWhateverTheAppName();

private:
    static QStringList columns(const QString &table);
    static void downgradeTo18();
};

QStringList DatabaseMigrationTest::columns(const QString &table)
{
    QStringList cols;
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next())
        cols << q.value(1).toString();
    return cols;
}

// Turns a freshly initialized database back into what a v18 build left.
void DatabaseMigrationTest::downgradeTo18()
{
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("DROP INDEX IF EXISTS idx_flights_device_uid")));
    for (const char *col : {"device_uid", "target_lat", "target_lon", "target_source"})
        QVERIFY2(q.exec(QStringLiteral("ALTER TABLE flights DROP COLUMN %1").arg(QLatin1String(col))),
                 qPrintable(q.lastError().text()));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM schema_version WHERE version > 18")));
}

// storedSchemaVersion() answered 0 during initialize(), so every launch
// re-ran every step and v12 rebuilt flights, zeroing its summary columns.
void DatabaseMigrationTest::testRestartDoesNotRerunMigrations()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("m.db"));
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize(path));
    {
        QSqlQuery q(QSqlDatabase::database());
        QVERIFY2(q.exec(QStringLiteral(
            "INSERT INTO flights (mode, started_at, max_ground_speed_ms, target_lat) "
            "VALUES ('Flight', '2026-10-22T10:00:00', 12.5, 9.03)")), qPrintable(q.lastError().text()));
    }
    db.reset();
    QFile::remove(path + QStringLiteral(".bak"));   // taken by the first, fresh-file migration
    QVERIFY(db.initialize(path));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT max_ground_speed_ms, target_lat FROM flights")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toDouble(), 12.5);
    QCOMPARE(q.value(1).toDouble(), 9.03);
    QVERIFY2(!QFile::exists(path + QStringLiteral(".bak")), "nothing to migrate, so no backup is taken");
}

// A v18 database gains the v19 flight columns.
void DatabaseMigrationTest::testUpgradeFrom18AddsFlightTargetColumns()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("m.db"));
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize(path));
    downgradeTo18();
    QVERIFY(!columns(QStringLiteral("flights")).contains(QStringLiteral("target_lat")));
    db.reset();
    QFile::remove(path + QStringLiteral(".bak"));

    QVERIFY(db.initialize(path));
    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());
    const QStringList cols = columns(QStringLiteral("flights"));
    for (const char *col : {"device_uid", "target_lat", "target_lon", "target_source"})
        QVERIFY2(cols.contains(QLatin1String(col)), col);
    QVERIFY(db.startupError().isEmpty());
    QVERIFY(QFile::exists(path + QStringLiteral(".bak")));
}

// A failing statement rolls the whole step back, the version is not
// recorded, and initialize() fails with a reason.
void DatabaseMigrationTest::testFailedStepRollsBackAndStops()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("m.db"));
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize(path));
    downgradeTo18();
    {
        // A table holding the v19 index name makes its CREATE INDEX fail,
        // after the step's ALTER TABLEs have already run.
        QSqlQuery q(QSqlDatabase::database());
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE idx_flights_device_uid (x INTEGER)")));
    }
    db.reset();

    QSignalSpy spy(&db, &DatabaseManager::startupErrorChanged);
    QVERIFY(!db.initialize(path));
    QCOMPARE(spy.count(), 1);
    QVERIFY2(db.startupError().contains(QStringLiteral("version 19")), qPrintable(db.startupError()));

    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 18);
    QVERIFY(!columns(QStringLiteral("flights")).contains(QStringLiteral("target_lat")));   // ALTERs rolled back
}

// One flight database: the app renames itself during start-up, and the
// database folder must not follow (records were split over two files).
// Only computes paths; never opens the real database.
void DatabaseMigrationTest::testOneDatabaseWhateverTheAppName()
{
    const QString original = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("PreflightQGroundControl Daily"));
    const QString early = DatabaseManager::defaultDatabasePath();
    QCoreApplication::setApplicationName(QStringLiteral("Skywin GCS"));
    const QString late = DatabaseManager::defaultDatabasePath();
    QCoreApplication::setApplicationName(original);

    QCOMPARE(early, late);
    QVERIFY2(late.endsWith(QStringLiteral("/Skywin GCS/uav_preflight_data.db")), qPrintable(late));
    // Unit tests stay in their own folder, away from flight records.
    QVERIFY2(original.endsWith(QStringLiteral("_unittest")), qPrintable(original));
    QVERIFY2(DatabaseManager::defaultDatabasePath() != late, qPrintable(DatabaseManager::defaultDatabasePath()));
}

// An unopenable path leaves a startup error for the banner.
void DatabaseMigrationTest::testUnopenablePathSetsStartupError()
{
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(!db.initialize(QStringLiteral("/proc/skywin-no-such-dir/m.db")));
    QVERIFY(!db.startupError().isEmpty());
    db.reset();
    QVERIFY(db.startupError().isEmpty());
}

// Fresh DB should initialize to the latest schema version
void DatabaseMigrationTest::testFreshDBSchemaVersion()
{
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize());

    int stored = db.storedSchemaVersion();
    int latest = db.schemaVersion();
    QCOMPARE(stored, latest);
}

// All expected tables should exist after initialization
void DatabaseMigrationTest::testAllTablesExist()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    QStringList expectedTables = {
        QStringLiteral("schema_version"),
        QStringLiteral("checklist_templates"),
        QStringLiteral("compliance_logs"),
        QStringLiteral("hardware_test_events"),
        QStringLiteral("motor_test_results"),
        QStringLiteral("maintenance_components"),
        QStringLiteral("vehicles"),
        QStringLiteral("vehicle_config"),
        QStringLiteral("batteries"),
        QStringLiteral("battery_cycles"),
        QStringLiteral("flight_sessions"),
        QStringLiteral("check_results"),
        QStringLiteral("check_config"),
        QStringLiteral("no_fly_zones"),
        QStringLiteral("zone_compliance_log"),
    };

    QSqlQuery q(QSqlDatabase::database());
    q.exec("SELECT name FROM sqlite_master WHERE type='table'");

    QStringList actualTables;
    while (q.next()) {
        actualTables.append(q.value(0).toString());
    }

    for (const QString &table : expectedTables) {
        QVERIFY2(actualTables.contains(table),
                 qPrintable(QStringLiteral("Missing table: %1").arg(table)));
    }
}

// Schema version should be consistent between schemaVersion() and storedSchemaVersion()
void DatabaseMigrationTest::testSchemaVersionConsistent()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());
    QVERIFY(db.schemaVersion() > 0);
}

// Flight sessions table should have expected columns
void DatabaseMigrationTest::testFlightSessionsTableStructure()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    QSqlQuery q(QSqlDatabase::database());
    q.exec("PRAGMA table_info(flight_sessions)");

    QStringList columns;
    while (q.next()) {
        columns.append(q.value(1).toString());
    }

    QVERIFY(columns.contains(QStringLiteral("id")));
    QVERIFY(columns.contains(QStringLiteral("device_uid")));
    QVERIFY(columns.contains(QStringLiteral("started_at")));
    QVERIFY(columns.contains(QStringLiteral("ended_at")));
    QVERIFY(columns.contains(QStringLiteral("duration_seconds")));
    QVERIFY(columns.contains(QStringLiteral("battery_serial")));
}

// Compliance logs table should exist and be writable
void DatabaseMigrationTest::testComplianceLogsTableStructure()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    const QString logId = QStringLiteral("migration-log-%1").arg(QUuid::createUuid().toString());
    bool saved = db.saveComplianceLog(
        logId,
        QStringLiteral("test-vehicle"),
        QStringLiteral("MultiRotor"),
        QStringLiteral("TestPilot"),
        QStringLiteral("{\"test\":true}"),
        QString());
    QVERIFY(saved);

    QString loaded = db.loadComplianceLog(logId);
    QVERIFY(!loaded.isEmpty());
}

// Check config table should support key-value storage
void DatabaseMigrationTest::testCheckConfigTableStructure()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    bool set = db.setCheckConfig(
        QStringLiteral("test.check"),
        QStringLiteral("testKey"),
        QStringLiteral("testValue"));
    QVERIFY(set);

    QString val = db.getCheckConfig(
        QStringLiteral("test.check"),
        QStringLiteral("testKey"));
    QCOMPARE(val, QStringLiteral("testValue"));
}

// Vehicle config table should persist JSON blobs
void DatabaseMigrationTest::testVehicleConfigTableStructure()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    // Register a vehicle first
    db.registerNewVehicle(QStringLiteral("migration-fp"), 1, 1,
                          QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"),
                          QStringLiteral("Quad"), QStringLiteral("4.5.2"),
                          99999, QStringLiteral("HW-UID-MIG"),
                          QStringLiteral("1.0"), QStringLiteral("Migration Test"),
                          QStringLiteral("HARDWARE_UID"));

    QString config = QStringLiteral("{\"test\":{\"value\":42}}");
    bool saved = db.saveVehicleConfig(QStringLiteral("migration-fp"), config);
    QVERIFY(saved);

    QString loaded = db.loadVehicleConfig(QStringLiteral("migration-fp"));
    QCOMPARE(loaded, config);
}

// No-fly zones table should exist and support CRUD
void DatabaseMigrationTest::testNoFlyZonesTableExists()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.initialize();

    int zoneId = db.insertZone(
        QStringLiteral("Test Zone"),
        QStringLiteral("Test description"),
        37.7749, -122.4194, 500.0,
        QStringLiteral("Test reason"));
    QVERIFY(zoneId > 0);

    auto zones = db.getAllZones();
    QVERIFY(zones.size() > 0);
}

UT_REGISTER_TEST(DatabaseMigrationTest)

#include "DatabaseMigrationTest.moc"
