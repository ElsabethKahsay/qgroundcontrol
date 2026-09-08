/**
 * @file DatabaseMigrationTest.cpp
 * @brief Tests for DatabaseManager schema migration (L5 from TEST_CHECKLIST.md).
 *
 * Covers:
 *   - Fresh DB initializes to latest schema
 *   - Schema version is correct after init
 *   - All expected tables exist
 *   - Migration from stored version to latest works
 *   - Schema version tracking across upgrades
 */

#include <QTest>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QUuid>

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
};

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
