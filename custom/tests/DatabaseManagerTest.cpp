#include <QTest>
#include <QTemporaryDir>
#include "core/SessionRecorder.h"
#include "core/GcsLinkDropsCheck.h"
#include "core/PowerTypeModel.h"
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlDatabase>
#include <QSettings>

#include "UnitTest.h"
#include "DatabaseManager.h"

class DatabaseManagerTest : public UnitTest {
    Q_OBJECT

private:
    QString initTempDb(DatabaseManager &db) {
        QString tmpPath = QDir::tempPath() + QStringLiteral("/test_db_XXXXXX.db");
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate(tmpPath);
        if (!tmpFile.open())
            qFatal("cannot create a temporary file for the test database");
        QString path = tmpFile.fileName();
        tmpFile.close();
        db.initialize(path);
        // A fresh file migrates from version 0 and leaves a versioned snapshot.
        QFile::remove(path + QStringLiteral(".v0.bak"));
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
    void testBatteryPackRegistry();
    void testBatteryCycleNeedsTwentyPercent();
    void testVehicleTargetSavedBeforeAnyFlight();
    void testRecorderFlightFollowsArmedState();
    void testRecorderLinkLossClosesAtLossTime();
    void testRecorderCrashRecoveryClosesBothTables();
    void testRecorderCheckResultsKeyedToSession();
    void testRecorderBatterySnapshotIsFrozen();
    void testRecorderEnergyFromMeasurementsOnly();
    void testRecorderGcsChargeForDeadSensor();
    void testRuntimeWriteErrorOnFirstFailure();
    void testRuntimeWriteErrorIgnoresExpectedFailures();
    void testRecorderWritesFlightHistoryAndHandovers();
    void testRecorderJoinsAFlightAlreadyArmed();
    void testBenchTestResultsLinkToFlightAndConnection();
    void testEnergyModelLearnsOnlyFromRealFlights();
    void testLinkDropsCountedSinceAppStart();
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
    void testOrphanedSessionRecoveredOnRelaunch();
    void testExportFlightsCsv();
    void testExportFlightDetailCsv();
    void testGetFlightStats();
    void testGetFlightStatsExcludesBenchAndGroundFlights();
    void testStatsCountFlightWithUnknownAltitude();
    void testIncrementVehicleFlightTimeUsesDeviceUid();
    void testHistoryVehicleKeyDoesNotMergeSysid();
    void testBackupBeforeMigration();
    void testBackupIncludesWritesStillInTheWal();
    void testEmptyNameUpsertAndSession();
    void testUpgradeFromV24();
    void testComplianceLogKeptWithoutVehicleDetails();
    void testRecorderHistoryEvents();
    void testVehicleMotorTableLink();
    void testVehicleReKeyMovesDependentsAndSettings();
    void testVehicleReKeyMergesIntoExistingVehicle();
    void testVehicleImportPreservesPackWeightSysidTargetAndIsAtomic();
    void testVehicleDeleteRefusedWhileItHasHistory();
    void testVehicleDeleteRemovesSetupWhenNoHistory();
    void testMigrationRetryKeepsOriginalBackup();
    void testLaterUpgradeMakesItsOwnBackup();
};

void DatabaseManagerTest::testInitialize()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_preflight_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
    bool ok = db.initialize(tmpFile.fileName());
    QVERIFY(ok);
}

void DatabaseManagerTest::testVehicleInsertAndRead()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_vehicle_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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

// Packs keep their rated config; editing one keeps its cycles.
void DatabaseManagerTest::testBatteryPackRegistry()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    QVERIFY(db.initialize(dir.filePath(QStringLiteral("packs.db"))));

    QVERIFY(!db.saveBatteryPack(QStringLiteral(" "), QString(), QStringLiteral("LiPo"), 6, 1, 5000));
    QVERIFY(db.saveBatteryPack(QStringLiteral("PK-07"), QStringLiteral("Wing A"), QStringLiteral("LiIon"), 4, 2, 3000));
    QVariantMap p = db.batteryPack(QStringLiteral("PK-07"));
    QCOMPARE(p.value(QStringLiteral("label")).toString(), QStringLiteral("Wing A"));
    QCOMPARE(p.value(QStringLiteral("chemistry")).toString(), QStringLiteral("LiIon"));
    QCOMPARE(p.value(QStringLiteral("ratedMah")).toInt(), 6000);   // 3000 mAh x 2P

    QVERIFY(db.recordBatteryFlight(11, QStringLiteral("PK-07"), 1500.0));
    QVERIFY(db.saveBatteryPack(QStringLiteral("PK-07"), QStringLiteral("Wing A2"), QStringLiteral("LiIon"), 4, 2, 3200));
    p = db.batteryPack(QStringLiteral("PK-07"));
    QCOMPARE(p.value(QStringLiteral("label")).toString(), QStringLiteral("Wing A2"));
    QCOMPARE(p.value(QStringLiteral("cycles")).toInt(), 1);
    QCOMPARE(db.listBatteryPacks().size(), 1);
}

// One cycle = a flight using >= 20 % of rated.
void DatabaseManagerTest::testBatteryCycleNeedsTwentyPercent()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    QVERIFY(db.initialize(dir.filePath(QStringLiteral("cycles.db"))));
    QVERIFY(db.saveBatteryPack(QStringLiteral("PK-1"), QString(), QStringLiteral("LiPo"), 6, 1, 5000));

    QVERIFY(!db.recordBatteryFlight(1, QStringLiteral("PK-1"), 999.0));    // 19.98 %
    QVERIFY(db.recordBatteryFlight(2, QStringLiteral("PK-1"), 1000.0));    // exactly 20 %
    QVERIFY(!db.recordBatteryFlight(2, QStringLiteral("PK-1"), 3000.0));   // same flight twice
    QVERIFY(!db.recordBatteryFlight(3, QStringLiteral("NOPE"), 3000.0));   // unregistered pack
    QCOMPARE(db.batteryPack(QStringLiteral("PK-1")).value(QStringLiteral("cycles")).toInt(), 1);

    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT used_mah FROM battery_cycles WHERE flight_session_id = 2")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toDouble(), 1000.0);
}

// The gimbal-page target is saved during preflight (no flight row
// yet), survives a restart of the database, and is copied onto the next
// flight session. clearVehicleTarget() forgets it (one vehicle, or all: the
// plugin clears them all when the GCS starts and one when its flight ends).
void DatabaseManagerTest::testVehicleTargetSavedBeforeAnyFlight()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("target.db"));
    QVERIFY(db.initialize(path));
    QVERIFY(!db.saveVehicleTarget(QStringLiteral("UID-T"), 9.03, 38.74, QStringLiteral("manual")));   // no vehicle row
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-T"), QStringLiteral("Wing"), QStringLiteral("ArduPilot"), QStringLiteral("Plane")));
    QVERIFY(db.vehicleTarget(QStringLiteral("UID-T")).isEmpty());
    QVERIFY(!db.saveVehicleTarget(QStringLiteral("UID-T"), 91.0, 38.74, QStringLiteral("manual")));
    QVERIFY(db.saveVehicleTarget(QStringLiteral("UID-T"), 9.03, 38.74, QStringLiteral("map center")));

    db.reset();
    QVERIFY(db.initialize(path));
    const QVariantMap t = db.vehicleTarget(QStringLiteral("UID-T"));
    QCOMPARE(t.value(QStringLiteral("lat")).toDouble(), 9.03);
    QCOMPARE(t.value(QStringLiteral("lon")).toDouble(), 38.74);
    QCOMPARE(t.value(QStringLiteral("source")).toString(), QStringLiteral("map center"));

    const int session = db.startFlightSession(QStringLiteral("UID-T"), QString());
    QVERIFY(session > 0);
    QVERIFY(db.copyVehicleTargetToSession(session, QStringLiteral("UID-T")));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT target_lat, target_source FROM flight_sessions WHERE id = %1").arg(session)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toDouble(), 9.03);
    QCOMPARE(q.value(1).toString(), QStringLiteral("map center"));

    // Cleared on the vehicle; the flight session keeps its copy.
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-U"), QStringLiteral("Quad"), QStringLiteral("ArduPilot"), QStringLiteral("Copter")));
    QVERIFY(db.saveVehicleTarget(QStringLiteral("UID-U"), 9.05, 38.81, QStringLiteral("typed")));
    QVERIFY(db.clearVehicleTarget(QStringLiteral("UID-T")));
    QVERIFY(db.vehicleTarget(QStringLiteral("UID-T")).isEmpty());
    QVERIFY(!db.vehicleTarget(QStringLiteral("UID-U")).isEmpty());   // only that vehicle
    QVERIFY(q.exec(QStringLiteral("SELECT target_lat FROM flight_sessions WHERE id = %1").arg(session)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toDouble(), 9.03);
    QVERIFY(db.clearVehicleTarget());                                   // all vehicles
    QVERIFY(db.vehicleTarget(QStringLiteral("UID-U")).isEmpty());

    // At a flight's end only a target saved before the flight started goes:
    // one saved for the next flight while it was closing stays.
    QVERIFY(db.saveVehicleTarget(QStringLiteral("UID-U"), 9.05, 38.81, QStringLiteral("typed")));
    QVERIFY(db.clearVehicleTarget(QStringLiteral("UID-U"), QDateTime::currentDateTimeUtc().addSecs(-60)));
    QVERIFY(!db.vehicleTarget(QStringLiteral("UID-U")).isEmpty());     // saved after the start: kept
    QVERIFY(db.clearVehicleTarget(QStringLiteral("UID-U"), QDateTime::currentDateTimeUtc().addSecs(60)));
    QVERIFY(db.vehicleTarget(QStringLiteral("UID-U")).isEmpty());      // saved before: gone
}

// ── SessionRecorder — no vehicle, no MockLink ──────────────

namespace {
struct RecorderFixture {
    QTemporaryDir dir;
    QString path;
    TestClock clock;
    SessionRecorder::Sample sample;
    RecorderFixture() : path(dir.filePath(QStringLiteral("rec.db"))) { MonotonicClock::setForTesting(&clock); }
    ~RecorderFixture() { MonotonicClock::setForTesting(nullptr); }
    bool init()
    {
        DatabaseManager &db = DatabaseManager::instance();
        return db.initialize(path)
            && db.upsertVehicle(QStringLiteral("UID-R"), QStringLiteral("Wing"), QStringLiteral("ArduPilot"), QStringLiteral("Plane"));
    }
    void wire(SessionRecorder &r) { r.setSampleProvider([this]() { return sample; }); }
};

QVariantMap row(const QString &sql)
{
    QVariantMap out;
    QSqlQuery q(QSqlDatabase::database());
    if (!q.exec(sql) || !q.next())
        return out;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i)
        out.insert(rec.fieldName(i), q.value(i));
    return out;
}

SessionRecorder::FlightStart start(const QString &source, bool bench = false)
{
    SessionRecorder::FlightStart s;
    s.armSource = source;
    s.gateAllowed = source == QLatin1String("gcs");
    s.mode = bench ? QStringLiteral("Testing") : QStringLiteral("Flight");
    s.bench = bench;
    return s;
}

// One row in every table keyed by the vehicle identity, so the re-key and
// delete tests both face the full set of dependants (including the three
// tables with a real FOREIGN KEY to vehicles).
bool seedVehicleDependents(const QString &key, const QString &fingerprint)
{
    auto run = [](const QString &sql) {
        QSqlQuery q(QSqlDatabase::database());
        return q.exec(sql);
    };
    const QString ts = QStringLiteral("2026-01-01T00:00:00Z");
    return run(QStringLiteral("INSERT INTO flight_sessions (device_uid, started_at) VALUES ('%1', '%2')").arg(key, ts))
        && run(QStringLiteral("INSERT INTO check_results (device_uid, check_id, status, evaluated_at) VALUES ('%1', 'c1', 'Pass', '%2')").arg(key, ts))
        && run(QStringLiteral("INSERT INTO vehicle_config (fingerprint, config_json) VALUES ('%1', '{}')").arg(fingerprint))
        && run(QStringLiteral("INSERT INTO flights (mode, started_at, device_uid) VALUES ('Flight', '%2', '%1')").arg(key, ts))
        && run(QStringLiteral("INSERT INTO connection_sessions (device_uid, opened_at) VALUES ('%1', '%2')").arg(key, ts))
        && run(QStringLiteral("INSERT INTO power_types (frame, prop_diameter_in, prop_pitch_in, cells, created_at) VALUES ('quad', 10, 5, 6, '%1')").arg(ts))
        && run(QStringLiteral("INSERT INTO vehicle_power_type (device_uid, power_type_id, linked_at) SELECT '%1', id, '%2' FROM power_types LIMIT 1").arg(key, ts))
        && run(QStringLiteral("INSERT INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES ('%1', 't1', '%2')").arg(key, ts))
        && run(QStringLiteral("INSERT INTO motor_table_flights (device_uid, table_id, started_at, ended_at, gcs_ah) VALUES ('%1', 't1', '%2', '%2', 1.0)").arg(key, ts))
        && run(QStringLiteral("INSERT INTO compliance_logs (log_id, vehicle_id, vehicle_type, checklist_json) VALUES ('log-%1', '%1', 'quad', '{}')").arg(key));
}

// The per-copter QSettings that VehicleProfileManager reads back, keyed by a
// test-specific key so tests cannot see each other's.
void seedVehicleSettings(const QString &key)
{
    QSettings s;
    s.setValue(QStringLiteral("MotorTable/packOhm/%1").arg(key), 0.055);
    s.setValue(QStringLiteral("MotorTable/waivedBy/%1").arg(key), QStringLiteral("Test Pilot"));
    s.setValue(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key), true);
    s.setValue(QStringLiteral("MotorTable/cruiseBand/%1/0").arg(key), 12.5);
    s.setValue(QStringLiteral("WindFromLean/k/%1").arg(key), 0.42);
    s.setValue(QStringLiteral("MissionEstimate/emptyKg/%1").arg(key), 9.75);
    s.sync();
}

bool allVehicleSettingsPresent(const QString &key)
{
    QSettings s;
    return s.contains(QStringLiteral("MotorTable/packOhm/%1").arg(key))
        && s.contains(QStringLiteral("MotorTable/waivedBy/%1").arg(key))
        && s.contains(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key))
        && s.contains(QStringLiteral("MotorTable/cruiseBand/%1/0").arg(key))
        && s.contains(QStringLiteral("WindFromLean/k/%1").arg(key))
        && s.contains(QStringLiteral("MissionEstimate/emptyKg/%1").arg(key));
}

bool anyVehicleSettingPresent(const QString &key)
{
    QSettings s;
    return s.contains(QStringLiteral("MotorTable/packOhm/%1").arg(key))
        || s.contains(QStringLiteral("MotorTable/waivedBy/%1").arg(key))
        || s.contains(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key))
        || s.contains(QStringLiteral("MotorTable/cruiseBand/%1/0").arg(key))
        || s.contains(QStringLiteral("WindFromLean/k/%1").arg(key))
        || s.contains(QStringLiteral("MissionEstimate/emptyKg/%1").arg(key));
}

void clearVehicleSettings(const QString &key)
{
    QSettings s;
    const QStringList prefixes{
        QStringLiteral("MotorTable/cruiseBand/%1").arg(key),
        QStringLiteral("MotorTable/waivedBy/%1").arg(key),
        QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key),
        QStringLiteral("MotorTable/packOhm/%1").arg(key),
        QStringLiteral("WindFromLean/k/%1").arg(key),
        QStringLiteral("MissionEstimate/emptyKg/%1").arg(key),
    };
    for (const QString &prefix : prefixes)
        s.remove(prefix);
    s.sync();
}

bool noForeignKeyViolations()
{
    QSqlQuery q(QSqlDatabase::database());
    return q.exec(QStringLiteral("PRAGMA foreign_key_check")) && !q.next();
}

int countRows(const QString &sql)
{
    return row(sql).value(QStringLiteral("n")).toInt();
}
} // namespace

// One flight per arm-to-disarm, whoever armed (an RC arm included), with
// its source, gate state and bench flag stored; durations from the clock.
void DatabaseManagerTest::testRecorderFlightFollowsArmedState()
{
    RecorderFixture f;
    QVERIFY(f.init());
    SessionRecorder rec(DatabaseManager::instance());
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QStringLiteral("Pilot A"), QStringLiteral("1.0"));
    QVERIFY(rec.connectionId() > 0);

    f.sample = {16.4, 100.0};
    rec.armed(start(QStringLiteral("external")));          // RC arm, no GCS ack
    const int first = rec.flightId();
    QVERIFY(first > 0);
    f.clock.advance(65000);
    f.sample = {15.1, 1400.0};
    const SessionRecorder::FlightEnd end = rec.disarmed();
    QCOMPARE(end.flightId, first);
    QCOMPARE(end.endReason, QStringLiteral("disarmed"));
    QCOMPARE(end.durationSec, 65.0);
    QCOMPARE(end.mahUsed, 1300.0);
    QVariantMap r = row(QStringLiteral("SELECT * FROM flight_sessions WHERE id = %1").arg(first));
    QCOMPARE(r.value(QStringLiteral("arm_source")).toString(), QStringLiteral("external"));
    QCOMPARE(r.value(QStringLiteral("gate_allowed")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("connection_session_id")).toInt(), rec.connectionId());
    QCOMPARE(r.value(QStringLiteral("end_reason")).toString(), QStringLiteral("disarmed"));
    QCOMPARE(r.value(QStringLiteral("duration_seconds")).toDouble(), 65.0);
    QCOMPARE(r.value(QStringLiteral("min_voltage")).toDouble(), 15.1);
    QVERIFY(!r.value(QStringLiteral("ended_at")).isNull());

    // A second arm is a second flight (the old code folded it into the first).
    rec.armed(start(QStringLiteral("force_token"), /*bench*/ true));
    QVERIFY(rec.flightId() > first);
    QCOMPARE(row(QStringLiteral("SELECT is_bench FROM flight_sessions WHERE id = %1").arg(rec.flightId()))
                 .value(QStringLiteral("is_bench")).toInt(), 1);
    const int connId = rec.connectionId();
    rec.connectionClosed(QStringLiteral("vehicle_removed"));
    QCOMPARE(row(QStringLiteral("SELECT end_reason FROM connection_sessions WHERE id = %1").arg(connId))
                 .value(QStringLiteral("end_reason")).toString(), QStringLiteral("vehicle_removed"));
    QCOMPARE(row(QStringLiteral("SELECT COUNT(*) AS n FROM flight_sessions WHERE ended_at IS NULL")).value(QStringLiteral("n")).toInt(), 0);
}

// Rule 4: link lost while armed keeps the flight open; back armed it goes on;
// back disarmed it closes as link_lost at the moment the link was lost.
void DatabaseManagerTest::testRecorderLinkLossClosesAtLossTime()
{
    RecorderFixture f;
    QVERIFY(f.init());
    SessionRecorder rec(DatabaseManager::instance());
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());

    rec.armed(start(QStringLiteral("gcs")));
    f.clock.advance(10000);
    rec.linkLost();
    rec.linkRestored(/*armed*/ true);
    QVERIFY(rec.flightOpen());                              // continues

    const int id = rec.flightId();
    f.clock.advance(20000);
    rec.linkLost();                                         // lost at t = 30 s
    f.clock.advance(45000);
    rec.linkRestored(/*armed*/ false);                      // came back disarmed
    QVERIFY(!rec.flightOpen());
    const QVariantMap r = row(QStringLiteral("SELECT end_reason, duration_seconds FROM flight_sessions WHERE id = %1").arg(id));
    QCOMPARE(r.value(QStringLiteral("end_reason")).toString(), QStringLiteral("link_lost"));
    QCOMPARE(r.value(QStringLiteral("duration_seconds")).toDouble(), 30.0);
    QCOMPARE(row(QStringLiteral("SELECT COUNT(*) AS n FROM session_events WHERE flight_session_id = %1").arg(id))
                 .value(QStringLiteral("n")).toInt(), 4);   // lost, restored, lost, restored
}

// Rows left open by a crash close at the last 10 s summary,
// for flights and connections alike.
void DatabaseManagerTest::testRecorderCrashRecoveryClosesBothTables()
{
    RecorderFixture f;
    QVERIFY(f.init());
    auto *rec = new SessionRecorder(DatabaseManager::instance());
    f.wire(*rec);
    rec->connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    f.sample = {16.0, 0.0};
    SessionRecorder::FlightStart st = start(QStringLiteral("gcs"));
    st.vehicleSysId = 1;
    rec->armed(st);
    const int flight = rec->flightId(), conn = rec->connectionId();
    f.clock.advance(20000);
    f.sample = {15.2, 500.0};
    rec->writeSummary();
    const QString summaryAt = row(QStringLiteral("SELECT last_summary_at FROM flight_sessions WHERE id = %1").arg(flight))
                                  .value(QStringLiteral("last_summary_at")).toString();

    DatabaseManager::instance().reset();                    // "crash": nothing gets closed
    delete rec;                                             // (writes are no-ops now)
    QVERIFY(DatabaseManager::instance().initialize(f.path));

    const QVariantMap fr = row(QStringLiteral("SELECT * FROM flight_sessions WHERE id = %1").arg(flight));
    QCOMPARE(fr.value(QStringLiteral("end_reason")).toString(), QStringLiteral("crash_recovered"));
    QCOMPARE(fr.value(QStringLiteral("ended_at")).toString(), summaryAt);
    QCOMPARE(fr.value(QStringLiteral("duration_seconds")).toDouble(), 20.0);
    QCOMPARE(fr.value(QStringLiteral("mah_used")).toDouble(), 500.0);
    QCOMPARE(row(QStringLiteral("SELECT end_reason FROM connection_sessions WHERE id = %1").arg(conn))
                 .value(QStringLiteral("end_reason")).toString(), QStringLiteral("crash_recovered"));
    // Its Flight History row closes at the same summary point (20 s), not at
    // "now" (which would count the time until this restart as flight time).
    const QVariantMap hist = row(QStringLiteral("SELECT f.duration_sec, f.ended_at FROM flights f "
                                                "JOIN flight_sessions fs ON fs.history_flight_id = f.id "
                                                "WHERE fs.id = %1").arg(flight));
    QVERIFY(!hist.isEmpty());
    QCOMPARE(hist.value(QStringLiteral("duration_sec")).toInt(), 20);
    QCOMPARE(hist.value(QStringLiteral("ended_at")).toString(), summaryAt);
}

// Check results land in check_results keyed to the connection session
// and the open flight, with the vehicles-row UID.
void DatabaseManagerTest::testRecorderCheckResultsKeyedToSession()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(!db.recordCheckResult(QStringLiteral("nav.gps.fix"), QStringLiteral("Failed"), QString()));   // no session
    SessionRecorder rec(db);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    QVERIFY(db.recordCheckResult(QStringLiteral("nav.gps.fix"), QStringLiteral("Failed"), QStringLiteral("no fix")));
    rec.armed(start(QStringLiteral("gcs")));
    QVERIFY(db.recordCheckResult(QStringLiteral("power.battery.voltage"), QStringLiteral("Warning"), QString()));

    QVariantMap r = row(QStringLiteral("SELECT * FROM check_results WHERE check_id = 'nav.gps.fix'"));
    QCOMPARE(r.value(QStringLiteral("device_uid")).toString(), QStringLiteral("UID-R"));
    QCOMPARE(r.value(QStringLiteral("connection_session_id")).toInt(), rec.connectionId());
    QCOMPARE(r.value(QStringLiteral("flight_session_id")).toInt(), 0);
    r = row(QStringLiteral("SELECT * FROM check_results WHERE check_id = 'power.battery.voltage'"));
    QCOMPARE(r.value(QStringLiteral("flight_session_id")).toInt(), rec.flightId());
}

// Each flight keeps the pack as it was at arm; editing the pack later
// changes no flight. Start/end values and the FC energy counter are stored.
void DatabaseManagerTest::testRecorderBatterySnapshotIsFrozen()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.saveBatteryPack(QStringLiteral("PK-9"), QStringLiteral("Wing B"), QStringLiteral("LiIon"), 4, 2, 3000));
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());

    SessionRecorder::FlightStart st = start(QStringLiteral("gcs"));
    st.batterySerial = QStringLiteral("PK-9");
    st.pack = db.batteryPack(QStringLiteral("PK-9"));
    f.sample = SessionRecorder::Sample{16.6, 0.0, 98, 2.0, 100.0};
    rec.armed(st);
    const int id = rec.flightId();
    QVERIFY(db.saveBatteryPack(QStringLiteral("PK-9"), QStringLiteral("Wing B"), QStringLiteral("LiIon"), 4, 2, 3500));   // edited mid-flight
    f.clock.advance(600000);
    f.sample = SessionRecorder::Sample{14.9, 2400.0, 41, 40.0, 5100.0};
    const SessionRecorder::FlightEnd end = rec.disarmed();
    QCOMPARE(end.whUsed, 38.0);                             // FC energy counter: 40 - 2

    const QVariantMap b = row(QStringLiteral("SELECT * FROM flight_batteries WHERE flight_session_id = %1").arg(id));
    QCOMPARE(b.value(QStringLiteral("pack_serial")).toString(), QStringLiteral("PK-9"));
    QCOMPARE(b.value(QStringLiteral("cell_mah")).toInt(), 3000);       // as at arm, not 3500
    QCOMPARE(b.value(QStringLiteral("rated_mah")).toInt(), 6000);
    QCOMPARE(b.value(QStringLiteral("chemistry")).toString(), QStringLiteral("LiIon"));
    QCOMPARE(b.value(QStringLiteral("start_voltage")).toDouble(), 16.6);
    QCOMPARE(b.value(QStringLiteral("end_voltage")).toDouble(), 14.9);
    QCOMPARE(b.value(QStringLiteral("start_percent")).toInt(), 98);
    QCOMPARE(b.value(QStringLiteral("end_percent")).toInt(), 41);
    QCOMPARE(b.value(QStringLiteral("mah_used")).toDouble(), 2400.0);
    QCOMPARE(b.value(QStringLiteral("wh_used")).toDouble(), 38.0);
    const QVariantMap fs = row(QStringLiteral("SELECT energy_consumed_wh, distance_m FROM flight_sessions WHERE id = %1").arg(id));
    QCOMPARE(fs.value(QStringLiteral("energy_consumed_wh")).toDouble(), 38.0);
    QCOMPARE(fs.value(QStringLiteral("distance_m")).toDouble(), 5000.0);
}

// No FC energy counter -> mAh x mean voltage; nothing known -> NULL, not 0.
void DatabaseManagerTest::testRecorderEnergyFromMeasurementsOnly()
{
    QCOMPARE(SessionRecorder::energyUsedWh(-1, -1, 2000.0, 16.0, 14.0), 30.0);
    QCOMPARE(SessionRecorder::energyUsedWh(5.0, 3.0, 2000.0, 16.0, 14.0), 30.0);   // counter reset: fall back
    QCOMPARE(SessionRecorder::energyUsedWh(-1, -1, -1, 16.0, 14.0), -1.0);

    RecorderFixture f;
    QVERIFY(f.init());
    SessionRecorder rec(DatabaseManager::instance());
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    rec.armed(start(QStringLiteral("external")));          // no pack, no battery data at all
    const int id = rec.flightId();
    rec.disarmed();
    const QVariantMap b = row(QStringLiteral("SELECT * FROM flight_batteries WHERE flight_session_id = %1").arg(id));
    QVERIFY(!b.isEmpty());
    QVERIFY(b.value(QStringLiteral("pack_serial")).isNull());
    QVERIFY(b.value(QStringLiteral("start_voltage")).isNull());
    QVERIFY(b.value(QStringLiteral("wh_used")).isNull());
    QVERIFY(b.value(QStringLiteral("mah_used")).isNull());
}

// The flight controller's current monitor reads 0 A, so its mAh counter stays
// 0 and its % stays 100. When the GCS counted the charge itself, that count
// (raw, the same one the charger is compared against) and the % it ended on
// go into the flight record; when it has no count either, both stay unknown
// and the columns are NULL rather than 0. With no source set, another vehicle
// still records the controller's own counter as before.
void DatabaseManagerTest::testRecorderGcsChargeForDeadSensor()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());

    f.sample = SessionRecorder::Sample{16.4, 0.0, 100, -1, 100.0};
    rec.setGcsChargeProvider([]() { return SessionRecorder::GcsCharge{12000.0, 82.0, true}; });
    rec.armed(start(QStringLiteral("external")));
    const int counted = rec.flightId();
    f.clock.advance(600000);
    f.sample = SessionRecorder::Sample{14.9, 0.0, 100, -1, 5100.0};
    const SessionRecorder::FlightEnd end = rec.disarmed();
    QCOMPARE(end.mahUsed, 12000.0);
    QVERIFY2(end.whUsed > 0.0, qPrintable(QString::number(end.whUsed)));
    QVariantMap b = row(QStringLiteral("SELECT * FROM flight_batteries WHERE flight_session_id = %1").arg(counted));
    // The FC's 100 % at arm is meaningless with a dead current sensor: the
    // GCS's count, from the resting voltage at arm, is the start charge.
    QCOMPARE(b.value(QStringLiteral("start_percent")).toInt(), 82);
    QCOMPARE(b.value(QStringLiteral("mah_used")).toDouble(), 12000.0);
    QCOMPARE(b.value(QStringLiteral("end_percent")).toInt(), 82);
    QVERIFY(b.value(QStringLiteral("wh_used")).toDouble() > 0.0);
    QCOMPARE(row(QStringLiteral("SELECT mah_used FROM flight_sessions WHERE id = %1").arg(counted))
                 .value(QStringLiteral("mah_used")).toDouble(), 12000.0);

    // Dead monitor but no GCS count (the GCS joined in flight): unknown, not 0.
    rec.setGcsChargeProvider([]() { return SessionRecorder::GcsCharge{-1.0, -1.0, true}; });
    f.sample = SessionRecorder::Sample{16.4, 0.0, 100, -1, -1};
    rec.armed(start(QStringLiteral("external")));
    const int unknown = rec.flightId();
    f.clock.advance(60000);
    const SessionRecorder::FlightEnd noCount = rec.disarmed();
    QCOMPARE(noCount.mahUsed, -1.0);
    QCOMPARE(noCount.whUsed, -1.0);
    b = row(QStringLiteral("SELECT * FROM flight_batteries WHERE flight_session_id = %1").arg(unknown));
    QVERIFY(b.value(QStringLiteral("start_percent")).isNull());
    QVERIFY(b.value(QStringLiteral("mah_used")).isNull());
    QVERIFY(b.value(QStringLiteral("wh_used")).isNull());
    QVERIFY(b.value(QStringLiteral("end_percent")).isNull());
    QVariantMap fs = row(QStringLiteral("SELECT * FROM flight_sessions WHERE id = %1").arg(unknown));
    QVERIFY(fs.value(QStringLiteral("mah_used")).isNull());
    QVERIFY(fs.value(QStringLiteral("energy_consumed_wh")).isNull());

    // No source set: the controller's counter is used, as before.
    rec.setGcsChargeProvider({});
    f.sample = SessionRecorder::Sample{16.4, 100.0};
    rec.armed(start(QStringLiteral("external")));
    const int fc = rec.flightId();
    f.clock.advance(60000);
    f.sample = SessionRecorder::Sample{15.1, 1400.0};
    rec.disarmed();
    QCOMPARE(row(QStringLiteral("SELECT mah_used FROM flight_batteries WHERE flight_session_id = %1").arg(fc))
                 .value(QStringLiteral("mah_used")).toDouble(), 1300.0);

    // The app always sets a source. For a copter whose current sensor works
    // the source does not apply, and the controller's own counter and % are
    // recorded, not "unknown".
    rec.setGcsChargeProvider([]() { return SessionRecorder::GcsCharge{}; });
    f.sample = SessionRecorder::Sample{16.4, 100.0, 90};
    rec.armed(start(QStringLiteral("external")));
    const int working = rec.flightId();
    f.clock.advance(60000);
    f.sample = SessionRecorder::Sample{15.1, 1400.0, 70};
    const SessionRecorder::FlightEnd fcEnd = rec.disarmed();
    QCOMPARE(fcEnd.mahUsed, 1300.0);
    b = row(QStringLiteral("SELECT * FROM flight_batteries WHERE flight_session_id = %1").arg(working));
    QCOMPARE(b.value(QStringLiteral("start_percent")).toInt(), 90);
    QCOMPARE(b.value(QStringLiteral("mah_used")).toDouble(), 1300.0);
    QCOMPARE(b.value(QStringLiteral("end_percent")).toInt(), 70);
}

// Flight History reads the flights table: every recorded flight gets a row
// there (operator optional), and trainer handovers attach to it.
// The GCS restarted (or connected) while the vehicle was already armed: no
// arm transition comes, so joinArmedFlight() opens the flight, counts time
// and energy from that moment (from the first sample that has battery data,
// if there was none yet), and refuses a second row or a row with no
// connection to key it to.
void DatabaseManagerTest::testRecorderJoinsAFlightAlreadyArmed()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    SessionRecorder rec(db);
    f.wire(rec);
    const QString countSql = QStringLiteral("SELECT COUNT(*) AS n FROM flight_sessions WHERE device_uid = 'UID-R'");

    QVERIFY(!rec.joinArmedFlight(start(QStringLiteral("gcs"))));        // no connection yet
    QCOMPARE(row(countSql).value(QStringLiteral("n")).toInt(), 0);

    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    // Joined right after start-up: no BATTERY_STATUS yet (voltage reads 0).
    f.sample.voltage = 0.0;
    f.sample.energyWh = -1;
    QVERIFY(rec.joinArmedFlight(start(QStringLiteral("gcs"))));
    const int id = rec.flightId();
    QVERIFY(id > 0);
    QVERIFY(!rec.joinArmedFlight(start(QStringLiteral("gcs"))));        // already open
    QCOMPARE(row(countSql).value(QStringLiteral("n")).toInt(), 1);

    // The first summary with battery data becomes the baseline.
    f.clock.advance(10000);
    f.sample.voltage = 16.0;
    f.sample.energyWh = 5.0;
    rec.writeSummary();
    f.clock.advance(50000);
    f.sample.energyWh = 7.5;
    rec.disarmed();
    const QVariantMap fs = row(QStringLiteral("SELECT * FROM flight_sessions WHERE id = %1").arg(id));
    QCOMPARE(fs.value(QStringLiteral("arm_source")).toString(), SessionRecorder::kArmSourceAlreadyArmed);
    QCOMPARE(fs.value(QStringLiteral("end_reason")).toString(), QStringLiteral("disarmed"));
    QCOMPARE(qRound(fs.value(QStringLiteral("duration_seconds")).toDouble()), 60);
    QCOMPARE(fs.value(QStringLiteral("energy_consumed_wh")).toDouble(), 2.5);
    QCOMPARE(fs.value(QStringLiteral("min_voltage")).toDouble(), 16.0);   // not the 0 V of no data
}

void DatabaseManagerTest::testRecorderWritesFlightHistoryAndHandovers()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Training"), QString(), QString());

    rec.trainerHandover(true, QStringLiteral("trainer switch, CH8"));   // before arm: connection event only
    SessionRecorder::FlightStart st = start(QStringLiteral("external"));
    st.mode = QStringLiteral("Training");
    st.vehicleSysId = 1;                                                // no operator selected
    f.sample.voltage = 16.5;
    f.sample.altitudeM = 0.0;
    f.sample.flightMode = QStringLiteral("STABILIZE");
    rec.armed(st);
    const int hist = rec.historyFlightId();
    QVERIFY(hist > 0);
    QCOMPARE(row(QStringLiteral("SELECT history_flight_id FROM flight_sessions WHERE id = %1").arg(rec.flightId()))
                 .value(QStringLiteral("history_flight_id")).toInt(), hist);

    f.clock.advance(30000);
    f.sample.altitudeM = 12.0;
    rec.trainerHandover(true, QStringLiteral("trainer switch, CH8"));
    f.clock.advance(60000);
    f.sample.altitudeM = 8.0;
    rec.trainerHandover(false, QStringLiteral("trainer switch, CH8"));
    f.clock.advance(30000);
    f.sample.voltage = 15.2;
    rec.disarmed();

    const QList<QVariantMap> h = db.getHandoverEventsForFlight(hist);
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.at(0).value(QStringLiteral("event_type")).toString(), QStringLiteral("HANDOVER_TO_STUDENT"));
    QCOMPARE(h.at(0).value(QStringLiteral("altitude_m")).toDouble(), 12.0);
    QCOMPARE(h.at(1).value(QStringLiteral("event_type")).toString(), QStringLiteral("HANDOVER_TO_INSTRUCTOR"));
    QCOMPARE(h.at(1).value(QStringLiteral("flight_mode")).toString(), QStringLiteral("STABILIZE"));

    const QVariantMap fl = db.getFlightById(hist);
    QCOMPARE(fl.value(QStringLiteral("mode")).toString(), QStringLiteral("TRAINING"));
    QCOMPARE(fl.value(QStringLiteral("duration_sec")).toInt(), 120);
    QCOMPARE(fl.value(QStringLiteral("max_altitude_m")).toDouble(), 12.0);
    QVERIFY(!fl.value(QStringLiteral("ended_at")).toString().isEmpty());
    QCOMPARE(row(QStringLiteral("SELECT COUNT(*) AS n FROM session_events WHERE event LIKE 'HANDOVER%'"))
                 .value(QStringLiteral("n")).toInt(), 3);
}

// Bench-test results were all written with flight -1 / 0 / NULL, so they could
// not be tied to any flight. They now link to the connection and, when a
// (bench) flight is open, to its Flight History row.
void DatabaseManagerTest::testBenchTestResultsLinkToFlightAndConnection()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Testing"), QString(), QString());

    QVERIFY(db.logHardwareTestStep(-1, QStringLiteral("gimbal.pitch"), 0, -30, -29, -3, 3, false,
                                   QStringLiteral("pass")));   // disarmed: connection only
    SessionRecorder::FlightStart st = start(QStringLiteral("force_token"), /*bench*/ true);
    st.vehicleSysId = 1;
    rec.armed(st);
    const int hist = rec.historyFlightId();
    QVERIFY(hist > 0);
    QVERIFY(db.logSurfaceTestResult(0, QStringLiteral("aileron"), 1, 1100, 1900, 1, QStringLiteral("PASS")));
    QVERIFY(db.logMotorTestResult(1, 1, 10, 2, 1150, 1148, 2, QStringLiteral("PASS")));

    QVariantMap r = row(QStringLiteral("SELECT flight_id, connection_session_id FROM hardware_test_events"));
    QCOMPARE(r.value(QStringLiteral("flight_id")).toInt(), 0);
    QCOMPARE(r.value(QStringLiteral("connection_session_id")).toInt(), rec.connectionId());
    r = row(QStringLiteral("SELECT flight_id, connection_session_id FROM surface_test_results"));
    QCOMPARE(r.value(QStringLiteral("flight_id")).toInt(), hist);
    QCOMPARE(r.value(QStringLiteral("connection_session_id")).toInt(), rec.connectionId());
    QCOMPARE(db.getSurfaceTestsForFlight(hist).size(), 1);           // shows in Flight History
    r = row(QStringLiteral("SELECT flight_id, connection_session_id FROM motor_test_results"));
    QCOMPARE(r.value(QStringLiteral("flight_id")).toInt(), hist);
    QCOMPARE(r.value(QStringLiteral("connection_session_id")).toInt(), rec.connectionId());
}

// The energy model learns only from real, complete flights: not bench runs,
// not flights cut off by link loss or a crash, and not flights that barely
// moved (a few metres would give an absurd Wh/km).
void DatabaseManagerTest::testEnergyModelLearnsOnlyFromRealFlights()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    auto add = [](int bench, const char *endReason, double wh, double distM, double durSec) {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral("INSERT INTO flight_sessions (device_uid, started_at, ended_at, end_reason, "
                                 "is_bench, energy_consumed_wh, distance_m, duration_seconds) "
                                 "VALUES ('UID-R', '2026-10-23T10:00:00Z', '2026-10-23T10:20:00Z', ?, ?, ?, ?, ?)"));
        q.addBindValue(QString::fromLatin1(endReason));
        q.addBindValue(bench);
        q.addBindValue(wh);
        q.addBindValue(distM);
        q.addBindValue(durSec);
        QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
    };
    add(0, "disarmed", 40.0, 10000.0, 1200.0);    // real flight: 4 Wh/km, 2 Wh/min
    add(1, "disarmed", 5.0, 600.0, 120.0);        // bench run
    add(0, "link_lost", 3.0, 2000.0, 300.0);      // cut off: energy incomplete
    add(0, "crash_recovered", 2.0, 1000.0, 200.0);
    add(0, "disarmed", 1.0, 10.0, 90.0);          // barely moved: 100 Wh/km
    add(0, "disarmed", 1.0, 800.0, 20.0);         // too short

    const auto m = db.getCalibratedPowerModel(QStringLiteral("UID-R"), 0.0, 1);
    QCOMPARE(m.dataPointCount, 1);
    QVERIFY(m.isCalibrated);
    QVERIFY(qAbs(m.whPerKm - 4.0) < 1e-9);
    QVERIFY(qAbs(m.hoverWhPerMin - 2.0) < 1e-9);
    QCOMPARE(db.getPowerCalibrationHistory(QStringLiteral("UID-R"), 50).size(), 1);
}

// The link-drop item counts what the recorder logged for this vehicle since
// the app started: link losses, plus connections that ended because the
// vehicle vanished (a USB drop) without a link loss first. One drop that was
// first a link loss and then a removal counts once.
void DatabaseManagerTest::testLinkDropsCountedSinceAppStart()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    GcsLinkDropsCheck check;
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);                 // no vehicle yet

    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QCOMPARE(db.linkDropsSinceStart(), 0);

    rec.linkLost();                                                  // drop 1: quiet link, came back
    rec.linkRestored(false);
    rec.linkLost();                                                  // drop 2: quiet, then removed
    rec.connectionClosed(QStringLiteral("vehicle_removed"));         //   (same drop, not counted twice)
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    rec.connectionClosed(QStringLiteral("vehicle_removed"));         // drop 3: USB pulled, no link_lost
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());
    QCOMPARE(db.linkDropsSinceStart(), 3);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY2(check.message().contains(QStringLiteral("3 times")), qPrintable(check.message()));

    // Another vehicle's drops are not this vehicle's.
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-X"), QStringLiteral("X"), QStringLiteral("ArduPilot"), QStringLiteral("Plane")));
    rec.connectionOpened(QStringLiteral("UID-X"), QStringLiteral("Flight"), QString(), QString());
    QCOMPARE(db.linkDropsSinceStart(), 0);
}

void DatabaseManagerTest::testDoubleIncrementGuard()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_dbl_inc_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    // v16 adds motor_thrust_table to vehicles; its accessor round-trip lives
    // in BatteryEstimatorTest::testMotorThrustTablePersistence. v17 adds the
    // battery pack config columns to vehicles; v18 adds the battery type.
    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());   // v19: flight target, v20: battery packs

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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    QVERIFY(tmpFile.open());
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
    // Form rows carry only a sysid; give them the canonical key the History
    // filter now uses (recorded flights get it from their session).
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("UPDATE flights SET device_uid = 'UID-NINE' WHERE id IN (%1, %2)")
                   .arg(f1).arg(f2)));
    QVERIFY(q.exec(QStringLiteral("UPDATE flights SET device_uid = 'UID-TEN' WHERE id = %1").arg(f3)));

    // Unfiltered: 3 flights, 1320s -> 22m 0s, 2 vehicles, 2 operators.
    QVariantMap stats = db.getFlightStats(QString(), QString(), QString(), QString());
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
    stats = db.getFlightStats(QString(), QString(), QString(), QStringLiteral("FLIGHT"));
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("20m 0s"));
    QCOMPARE(stats.value(QStringLiteral("vehicleCount")).toInt(), 1);
    QCOMPARE(stats.value(QStringLiteral("avgPassRate")).toInt(), 100);
    QCOMPARE(stats.value(QStringLiteral("anomalyRate")).toInt(), 100);

    // Filtered to one vehicle key only: 2 flights.
    stats = db.getFlightStats(QString(), QString(), QStringLiteral("UID-NINE"), QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 2);

    // Filtered to the other vehicle: 1 flight, 120s -> 2m 0s.
    stats = db.getFlightStats(QString(), QString(), QStringLiteral("UID-TEN"), QString());
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("2m 0s"));
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 1);
}

// The stats banner counts real flights only: a bench arm and a flight that
// never got airborne are left out, while both rows stay in Flight History.
void DatabaseManagerTest::testGetFlightStatsExcludesBenchAndGroundFlights()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryFile tmpFile(QDir::tempPath() + QStringLiteral("/test_stats_bench_XXXXXX.db"));
    QVERIFY(tmpFile.open());
    const QString path = tmpFile.fileName();
    QVERIFY(db.initialize(path));
    QFile::remove(path + QStringLiteral(".v0.bak"));
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-STATS"), QStringLiteral("Stats Rig"),
                             QStringLiteral("ArduPilot"), QStringLiteral("Quad")));

    const int op = db.insertOperator(QStringLiteral("Pilot"), QStringLiteral("Pilot"));
    QVERIFY(op > 0);

    auto addFlight = [&](bool bench, double maxAlt) {
        const int session = db.openFlightRecord(0, QStringLiteral("UID-STATS"), QString(), 0.0,
                                                bench ? QStringLiteral("force_token") : QStringLiteral("external"),
                                                true, QStringLiteral("FLIGHT"), bench);
        QVERIFY(session > 0);
        const int history = db.openHistoryFlight(session, op, 1, QStringLiteral("FLIGHT"),
                                                 bench ? QStringLiteral("bench") : QStringLiteral("real"),
                                                 QString());
        QVERIFY(history > 0);
        QVERIFY(db.closeFlight(history, 600, maxAlt, 11.0, 13.5, 0,
                               0, 0, 0, 0, 0, 0, 0, 0));
        QVERIFY(db.closeFlightRecord(session, 600, QStringLiteral("disarmed"), QString(), 11.0, -1));
    };
    addFlight(false, 10.0);
    addFlight(true, 10.0);
    addFlight(false, PowerTypeModel::kAirborneAltM - 0.1);

    const QVariantMap stats = db.getFlightStats(QString(), QString(), QString(), QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 1);
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("10m 0s"));

    const QVariantMap history = db.queryFlights(0, QString(), QString(), QString(), 0,
                                                QString(), QString(), QStringLiteral("date_desc"), 50);
    QCOMPARE(history.value(QStringLiteral("totalCount")).toInt(), 3);

    db.reset();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".v0.bak"));
}

// A link lost before any altitude sample leaves max_altitude_m unknown. That
// must be stored as NULL and still count as a flight; a known low altitude
// (below the airborne threshold) does not count. All three stay in History.
void DatabaseManagerTest::testStatsCountFlightWithUnknownAltitude()
{
    DatabaseManager &db = DatabaseManager::instance();
    RecorderFixture f;
    QVERIFY(f.init());
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"),
                         QStringLiteral("Pilot"), QStringLiteral("1.0"));
    f.sample = {16.4, 100.0};   // no altitude sample: m_maxAltitude stays NaN
    rec.armed(start(QStringLiteral("external")));
    QVERIFY(rec.flightId() > 0);
    f.clock.advance(65000);
    rec.disarmed();

    const QVariantMap unknownRow = row(QStringLiteral(
        "SELECT max_altitude_m FROM flights ORDER BY id DESC LIMIT 1"));
    QVERIFY2(unknownRow.value(QStringLiteral("max_altitude_m")).isNull(),
             "an unknown altitude must be stored as NULL, not 0");

    const int op = db.insertOperator(QStringLiteral("Pilot"), QStringLiteral("Pilot"));
    QVERIFY(op > 0);
    const int low = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(),
                                  QString(), QString(), QString());
    QVERIFY(low > 0);
    QVERIFY(db.closeFlight(low, 60, 0.4, 11.0, 13.5, 0, 0, 0, 0, 0));
    const int high = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(),
                                   QString(), QString(), QString());
    QVERIFY(high > 0);
    QVERIFY(db.closeFlight(high, 60, 12.0, 11.0, 13.5, 0, 0, 0, 0, 0));

    // Unknown (65 s) + high (60 s) count; low (0.4 m, 60 s) does not.
    const QVariantMap stats = db.getFlightStats(QString(), QString(), QString(), QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("totalHoursStr")).toString(), QStringLiteral("2m 5s"));

    const QVariantMap history = db.queryFlights(0, QString(), QString(), QString(), 0,
                                                QString(), QString(), QStringLiteral("date_desc"), 50);
    QCOMPARE(history.value(QStringLiteral("totalCount")).toInt(), 3);
}

// Vehicle totals are keyed by the canonical device_uid. A legacy fingerprint
// is only a lookup key, never the identity the totals accumulate onto.
void DatabaseManagerTest::testIncrementVehicleFlightTimeUsesDeviceUid()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vehicle-time.db"));
    QVERIFY(db.initialize(path));
    QFile::remove(path + QStringLiteral(".v0.bak"));
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-TIME"), QStringLiteral("Time Rig"),
                             QStringLiteral("ArduPilot"), QStringLiteral("Quad")));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("UPDATE vehicles SET fingerprint = 'FP-TIME' WHERE device_uid = 'UID-TIME'")));

    QVERIFY(db.incrementVehicleFlightTime(QStringLiteral("UID-TIME"), 120));
    QVariantMap v = row(QStringLiteral("SELECT total_flight_time_sec, total_flight_count, total_flight_hours "
                                       "FROM vehicles WHERE device_uid = 'UID-TIME'"));
    QCOMPARE(v.value(QStringLiteral("total_flight_time_sec")).toInt(), 120);
    QCOMPARE(v.value(QStringLiteral("total_flight_count")).toInt(), 1);
    QCOMPARE(v.value(QStringLiteral("total_flight_hours")).toDouble(), 120.0 / 3600.0);

    // The same call with a fingerprint must not touch the vehicle.
    QVERIFY(db.incrementVehicleFlightTime(QStringLiteral("FP-TIME"), 60));
    v = row(QStringLiteral("SELECT total_flight_time_sec, total_flight_count FROM vehicles "
                           "WHERE device_uid = 'UID-TIME'"));
    QCOMPARE(v.value(QStringLiteral("total_flight_time_sec")).toInt(), 120);
    QCOMPARE(v.value(QStringLiteral("total_flight_count")).toInt(), 1);

    db.reset();
    QFile::remove(path + QStringLiteral(".v0.bak"));
}

// History keys a row by its flight session's device uid (falling back to the
// flights row's own uid). Two airframes on the same MAVLink sysid must not
// merge; a row with no key is still visible under "All vehicles".
void DatabaseManagerTest::testHistoryVehicleKeyDoesNotMergeSysid()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("history-key.db"));
    QVERIFY(db.initialize(path));
    QFile::remove(path + QStringLiteral(".v0.bak"));
    const int op = db.insertOperator(QStringLiteral("Pilot"), QStringLiteral("Pilot"));
    QVERIFY(op > 0);
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-A"), QStringLiteral("Alpha"),
                             QStringLiteral("ArduPilot"), QStringLiteral("Quad")));
    QVERIFY(db.upsertVehicle(QStringLiteral("UID-B"), QStringLiteral("Beta"),
                             QStringLiteral("ArduPilot"), QStringLiteral("Quad")));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("UPDATE vehicles SET sysid = 1 WHERE device_uid IN ('UID-A','UID-B')")));

    auto addSessionFlight = [&](const QString &uid, const QString &purpose) {
        const int session = db.openFlightRecord(0, uid, QString(), 0.0, QStringLiteral("external"),
                                                true, QStringLiteral("FLIGHT"), false);
        QVERIFY(session > 0);
        const int history = db.openHistoryFlight(session, op, 1, QStringLiteral("FLIGHT"),
                                                 purpose, QString());
        QVERIFY(history > 0);
        // Stats only count flights that got airborne.
        QVERIFY(q.exec(QStringLiteral("UPDATE flights SET max_altitude_m = 100 WHERE id = %1")
                       .arg(history)));
    };
    addSessionFlight(QStringLiteral("UID-A"), QStringLiteral("alpha"));
    addSessionFlight(QStringLiteral("UID-B"), QStringLiteral("beta"));

    const int unknown = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QStringLiteral("unknown"),
                                      QString(), QString(), QString());
    QVERIFY(unknown > 0);
    const int legacy = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QStringLiteral("legacy"),
                                     QString(), QString(), QString());
    QVERIFY(legacy > 0);
    QVERIFY(q.exec(QStringLiteral("UPDATE flights SET device_uid = 'UID-C' WHERE id = %1").arg(legacy)));
    // These two rows are about vehicle keys, not airborne flights; keep them
    // below the threshold now that an unknown (NULL) altitude counts too.
    QVERIFY(q.exec(QStringLiteral("UPDATE flights SET max_altitude_m = 0.4 WHERE id IN (%1, %2)")
                   .arg(unknown).arg(legacy)));

    auto query = [&](const QString &key) {
        return db.queryFlights(0, QString(), QString(), key, 0,
                               QString(), QString(), QStringLiteral("date_desc"), 50);
    };
    const QVariantMap byA = query(QStringLiteral("UID-A"));
    QCOMPARE(byA.value(QStringLiteral("totalCount")).toInt(), 1);
    const QVariantList aRows = byA.value(QStringLiteral("rows")).toList();
    QCOMPARE(aRows.size(), 1);
    QCOMPARE(aRows.at(0).toMap().value(QStringLiteral("notes")).toString(), QStringLiteral("alpha"));
    QCOMPARE(aRows.at(0).toMap().value(QStringLiteral("vehicle_name")).toString(), QStringLiteral("Alpha"));

    const QVariantMap byB = query(QStringLiteral("UID-B"));
    QCOMPARE(byB.value(QStringLiteral("totalCount")).toInt(), 1);
    QCOMPARE(byB.value(QStringLiteral("rows")).toList().at(0).toMap()
                 .value(QStringLiteral("vehicle_name")).toString(), QStringLiteral("Beta"));

    // The fallback to the form row's own uid finds a row with no session.
    QCOMPARE(query(QStringLiteral("UID-C")).value(QStringLiteral("totalCount")).toInt(), 1);

    // With no filter, all four rows remain visible; the unkeyed one has no
    // vehicle name (it is an unknown vehicle, not merged into either one).
    const QVariantMap all = query(QString());
    QCOMPARE(all.value(QStringLiteral("totalCount")).toInt(), 4);
    for (const QVariant &v : all.value(QStringLiteral("rows")).toList()) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("purpose")).toString() == QStringLiteral("unknown"))
            QVERIFY(r.value(QStringLiteral("vehicle_name")).toString().isEmpty());
    }

    const QVariantMap stats = db.getFlightStats(QString(), QString(), QStringLiteral("UID-A"), QString());
    QCOMPARE(stats.value(QStringLiteral("totalFlights")).toInt(), 1);
    QCOMPARE(stats.value(QStringLiteral("vehicleCount")).toInt(), 1);

    // Both airborne sessions used sysid 1, but they are two distinct vehicles.
    const QVariantMap allStats = db.getFlightStats(QString(), QString(), QString(), QString());
    QCOMPARE(allStats.value(QStringLiteral("totalFlights")).toInt(), 2);
    QCOMPARE(allStats.value(QStringLiteral("vehicleCount")).toInt(), 2);

    db.reset();
    QFile::remove(path + QStringLiteral(".v0.bak"));
}

void DatabaseManagerTest::testBackupBeforeMigration()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_backup_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
    tmpFile.close();
    const QString path = tmpFile.fileName();

    // First init writes schema and triggers migration from version 0, so the
    // snapshot is named after that version.
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".v0.bak")));

    // A second init is a no-op (already initialized), so the backup file is
    // still the migration-time snapshot.
    QVERIFY(db.initialize(path));

    // Seed a flight, then re-initialize via reset() so the on-disk DB (now at
    // latest schema) goes through backup again before a (no-op) migration.
    int opId = db.insertOperator(QStringLiteral("Ivy"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);
    db.reset();
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".v0.bak")));

    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".v0.bak"));
}

// The database runs in WAL mode: recent writes can still sit in the -wal
// file when the migration backup is taken, for instance while another
// connection holds a read open (a checkpoint cannot pass it). The backup must
// still contain them, so it is written through SQLite, not copied as a file.
void DatabaseManagerTest::testBackupIncludesWritesStillInTheWal()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryFile tmpFile(QDir::tempPath() + QStringLiteral("/test_walbak_XXXXXX.db"));
    QVERIFY(tmpFile.open());
    tmpFile.close();
    const QString path = tmpFile.fileName();
    QVERIFY(db.initialize(path));
    QFile::remove(path + QStringLiteral(".v0.bak"));
    const QString previousBak = path + QStringLiteral(".v%1.bak").arg(db.schemaVersion() - 1);

    {
        // A reader whose snapshot predates the writes below keeps them in the WAL.
        QSqlDatabase reader = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("walReader"));
        reader.setDatabaseName(path);
        QVERIFY(reader.open());
        QSqlQuery r(reader);
        QVERIFY(r.exec(QStringLiteral("BEGIN")));
        QVERIFY(r.exec(QStringLiteral("SELECT COUNT(*) FROM operators")) && r.next());

        QVERIFY(db.insertOperator(QStringLiteral("Only In The WAL"), QStringLiteral("pilot")) > 0);
        // One version back, so the next start migrates and takes the backup.
        QSqlQuery w(QSqlDatabase::database());
        QVERIFY(w.exec(QStringLiteral("DELETE FROM schema_version WHERE version = (SELECT MAX(version) FROM schema_version)")));
        db.reset();
        QVERIFY(db.initialize(path));

        r.exec(QStringLiteral("COMMIT"));
        r.finish();
        reader.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("walReader"));

    QVERIFY(QFile::exists(previousBak));
    {
        QSqlDatabase bak = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("walBackup"));
        bak.setDatabaseName(previousBak);
        QVERIFY(bak.open());
        QSqlQuery b(bak);
        QVERIFY2(b.exec(QStringLiteral("SELECT COUNT(*) FROM operators WHERE name = 'Only In The WAL'")) && b.next(),
                 qPrintable(b.lastError().text()));
        QCOMPARE(b.value(0).toInt(), 1);
        b.finish();
        bak.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("walBackup"));

    db.reset();
    QFile::remove(path + QStringLiteral(".v0.bak"));
    QFile::remove(previousBak);
}

void DatabaseManagerTest::testEmptyNameUpsertAndSession()
{
    // Regression: VehicleProfileManager upserts with an empty
    // friendlyName ({}). friendly_name is NOT NULL, and startFlightSession has
    // an FK on device_uid — the empty-name path must still succeed and never
    // orphan the flight session.
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_empty_name_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
    QVERIFY(db.initialize(tmpFile.fileName()));

    // Empty friendly name + empty motor layout + zero GPS — the exact write
    // signature VehicleProfileManager uses for a brand-new vehicle.
    QVERIFY(db.upsertVehicleEx(QStringLiteral("UID-EMPTY-NAME"), QString(),
                               QStringLiteral("ardupilotmega"), QStringLiteral("quad"),
                               -1, -1, 0, QString(), 0.0, 0.0));

    // Session insert must not hit the NOT NULL / FK failure and must return a
    // usable session id.
    int sessionId = db.startFlightSession(QStringLiteral("UID-EMPTY-NAME"), QString(), 0.0);
    QVERIFY(sessionId > 0);

    // The vehicle row is readable afterward.
    QString vehicle = db.getVehicle(QStringLiteral("UID-EMPTY-NAME"));
    QVERIFY(!vehicle.isEmpty());

    // The flight session row exists (FK satisfied → device_uid present).
    QString sessions = db.getFlightSessions(QStringLiteral("UID-EMPTY-NAME"), 5);
    QVERIFY(!sessions.isEmpty());
    QVERIFY(sessions != QStringLiteral("[]"));
    QVERIFY(QJsonDocument::fromJson(sessions.toUtf8()).array().size() >= 1);
}

void DatabaseManagerTest::testOrphanedSessionRecoveredOnRelaunch()
{
    // Unexpected-exit-and-relaunch, at the level this codebase actually supports.
    // There is no child process to spawn or kill, so the
    // process boundary is simulated where GCS state actually persists: db.reset()
    // drops every bit of in-memory state and closes the connection, exactly what
    // an abnormal exit does to anything not on disk, and initialize() then stands
    // in for a relaunch.
    //
    // The gap this closes versus testRecoverOrphanedSessions(): that test calls
    // recoverOrphanedSessions() itself. It proves the repair SQL works, but not
    // that a relaunch performs it. If the startup call in initialize() were ever
    // dropped or moved, this test fails while that one keeps passing.
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_relaunch_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    QVERIFY(tmpFile.open());
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opId = db.insertOperator(QStringLiteral("Robin"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);

    // Arm and go mid-flight, then never close it: the state a crash leaves behind.
    int armedFlight = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                    QString(), QString(), QString());
    QVERIFY(armedFlight > 0);
    QVERIFY(db.setFlightArmedAt(armedFlight, QDateTime::currentDateTimeUtc().addSecs(-90)));

    int preArmFlight = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                     QString(), QString(), QString());
    QVERIFY(preArmFlight > 0);

    // Armed, then disarmed cleanly, and only the *recording* of the close was
    // lost. Duration must come from armed_at -> disarmed_at, not from
    // armed_at -> "now", or a flight flown at 10:05 and closed at 14:00 is
    // recorded as nearly four hours of flight time.
    const QDateTime armTime = QDateTime::currentDateTimeUtc().addSecs(-3600);
    const QDateTime disarmTime = armTime.addSecs(45);
    int disarmedFlight = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                       QString(), QString(), QString());
    QVERIFY(disarmedFlight > 0);
    QVERIFY(db.setFlightArmedAt(disarmedFlight, armTime));
    QVERIFY(db.setFlightDisarmedAt(disarmedFlight, disarmTime));

    // Confirm both are genuinely open before the simulated exit, so a passing
    // assertion afterwards cannot be vacuously true.
    QVERIFY(db.getFlightById(armedFlight).value(QStringLiteral("ended_at")).toString().isEmpty());
    QVERIFY(db.getFlightById(preArmFlight).value(QStringLiteral("ended_at")).toString().isEmpty());
    QVERIFY(db.getFlightById(disarmedFlight).value(QStringLiteral("ended_at")).toString().isEmpty());

    // Unexpected exit: all in-memory state gone.
    db.reset();

    // Relaunch. Note that recoverOrphanedSessions() is deliberately NOT called
    // here - initialize() must do it on its own.
    QVERIFY(db.initialize(tmpFile.fileName()));

    // The armed session is closed out, not left open forever, and records how
    // long it was actually armed rather than inflating to the wall clock.
    auto armed = db.getFlightById(armedFlight);
    QVERIFY(!armed.value(QStringLiteral("ended_at")).toString().isEmpty());
    QVERIFY(armed.value(QStringLiteral("duration_sec")).toInt() >= 88);
    QVERIFY(armed.value(QStringLiteral("notes")).toString().contains(QStringLiteral("interrupted")));

    // The pre-arm session is also closed, at zero duration.
    auto preArm = db.getFlightById(preArmFlight);
    QVERIFY(!preArm.value(QStringLiteral("ended_at")).toString().isEmpty());
    QCOMPARE(preArm.value(QStringLiteral("duration_sec")).toInt(), 0);

    // The armed-then-disarmed session records the real armed window (~45s), not
    // the ~1h between arming and this relaunch.
    auto disarmed = db.getFlightById(disarmedFlight);
    QVERIFY(!disarmed.value(QStringLiteral("ended_at")).toString().isEmpty());
    const int disarmedDuration = disarmed.value(QStringLiteral("duration_sec")).toInt();
    QVERIFY2(disarmedDuration >= 44 && disarmedDuration <= 46,
             qPrintable(QStringLiteral("expected ~45s armed window, got %1s")
                            .arg(disarmedDuration)));

    // Not left inconsistent: recovery must close EVERY row with a NULL ended_at,
    // not just the three this test created, and must not stamp a state that is
    // internally impossible. Read the file through a separate connection so the
    // check is about what is actually on disk rather than anything DatabaseManager
    // is holding.
    {
        QSqlDatabase verifyDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          QStringLiteral("relaunch_verify_conn"));
        verifyDb.setDatabaseName(tmpFile.fileName());
        QVERIFY(verifyDb.open());
        QSqlQuery q(verifyDb);

        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM flights WHERE ended_at IS NULL")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);

        // A negative duration would log a flight that never happened as flying.
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM flights WHERE duration_sec < 0")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);

        // A disarmed_at before armed_at is the only way the duration above could
        // go negative, and would mean recovery stamped the window backwards.
        QVERIFY(q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM flights WHERE armed_at IS NOT NULL "
            "AND disarmed_at IS NOT NULL "
            "AND julianday(disarmed_at) < julianday(armed_at)")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);

        verifyDb.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("relaunch_verify_conn"));

    // Relaunch must be idempotent: a second exit/relaunch cycle finds nothing
    // left open, rather than re-stamping the recovered rows.
    const QString firstEnded = armed.value(QStringLiteral("ended_at")).toString();
    const int firstDuration = armed.value(QStringLiteral("duration_sec")).toInt();
    db.reset();
    QVERIFY(db.initialize(tmpFile.fileName()));

    auto again = db.getFlightById(armedFlight);
    QCOMPARE(again.value(QStringLiteral("ended_at")).toString(), firstEnded);
    QCOMPARE(again.value(QStringLiteral("duration_sec")).toInt(), firstDuration);
    // And the marker is not appended a second time.
    QCOMPARE(again.value(QStringLiteral("notes")).toString()
                 .count(QStringLiteral("interrupted")), 1);

    db.reset();
}

// A database written before version 25 gains the (no longer used)
// power-type tables and flight columns when it is opened, and then the
// motor-table link (v26): every migration still runs in order.
void DatabaseManagerTest::testUpgradeFromV24()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("v24.db"));
    QVERIFY(db.initialize(path));
    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());
    db.reset();

    // Roll the file back to its v24 shape.
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v24rollback"));
        raw.setDatabaseName(path);
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral("DROP TABLE vehicle_motor_table")));
        QVERIFY(q.exec(QStringLiteral("DROP TABLE vehicle_power_type")));
        QVERIFY(q.exec(QStringLiteral("DROP TABLE power_types")));
        QVERIFY(q.exec(QStringLiteral("DROP INDEX idx_flight_sessions_power_type")));
        for (const char *col : { "power_type_id", "hover_min_empty", "charger_mah", "learning_state" })
            QVERIFY2(q.exec(QStringLiteral("ALTER TABLE flight_sessions DROP COLUMN %1").arg(QLatin1String(col))),
                     qPrintable(q.lastError().text()));
        QVERIFY(q.exec(QStringLiteral("DELETE FROM schema_version WHERE version >= 25")));
        raw.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v24rollback"));

    QVERIFY(db.initialize(path));
    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());
    QVERIFY(db.linkVehicleMotorTable(QStringLiteral("UID-V24"), QStringLiteral("m6215-320kv-msc22x7.8-6s")));
    QSqlQuery cols(QSqlDatabase::database());
    QVERIFY(cols.exec(QStringLiteral("SELECT COUNT(*) FROM power_types")));
    QVERIFY(cols.exec(QStringLiteral("PRAGMA table_info(flight_sessions)")));
    QStringList names;
    while (cols.next())
        names << cols.value(1).toString();
    for (const char *col : { "power_type_id", "hover_min_empty", "charger_mah", "learning_state" })
        QVERIFY2(names.contains(QLatin1String(col)), col);

    QFile::remove(path + QStringLiteral(".v0.bak"));
    QFile::remove(path + QStringLiteral(".v24.bak"));
}

// The flight-end record is written with the device UID but no vehicle type
// (the vehicle is often gone by then: link lost, app exit). compliance_logs
// requires both, so the record must be kept under "unknown", not dropped.
void DatabaseManagerTest::testComplianceLogKeptWithoutVehicleDetails()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryDir dir;
    QVERIFY(db.initialize(dir.filePath(QStringLiteral("compliance.db"))));

    QVERIFY(db.saveComplianceLog(QStringLiteral("session-60"), QStringLiteral("UAV-1-35b13a25"), QString(),
                                 QString(), QStringLiteral("{\"endReason\":\"app_exit\"}"), QString()));
    QVERIFY(db.saveComplianceLog(QStringLiteral("session-61"), QString(), QString(), QString(), QString(), QString()));

    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT log_id, vehicle_id, vehicle_type FROM compliance_logs ORDER BY log_id")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("session-60"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("UAV-1-35b13a25"));
    QCOMPARE(q.value(2).toString(), QStringLiteral("unknown"));
    QVERIFY(q.next());
    QCOMPARE(q.value(1).toString(), QStringLiteral("unknown"));
}

// Every recorded flight's Flight History row gets its arm (with who armed)
// and its disarm (with why the flight ended) as events, for the page's
// "Events" tab.
void DatabaseManagerTest::testRecorderHistoryEvents()
{
    RecorderFixture f;
    QVERIFY(f.init());
    DatabaseManager &db = DatabaseManager::instance();
    SessionRecorder rec(db);
    f.wire(rec);
    rec.connectionOpened(QStringLiteral("UID-R"), QStringLiteral("Flight"), QString(), QString());

    f.sample = SessionRecorder::Sample{16.4, 0.0, 99, -1, -1};
    rec.armed(start(QStringLiteral("external")));
    const int historyId = rec.historyFlightId();
    QVERIFY(historyId > 0);
    QCOMPARE(db.recordingHistoryFlightId(), historyId);
    f.clock.advance(60000);
    rec.disarmed();
    QCOMPARE(db.recordingHistoryFlightId(), -1);

    QStringList events;
    for (const QVariantMap &e : db.getTelemetryEventsForFlight(historyId))
        events << e.value(QStringLiteral("event_type")).toString() + QLatin1Char(':')
                      + e.value(QStringLiteral("triggered_by")).toString();
    QVERIFY2(events.contains(QStringLiteral("ARM:external")) && events.contains(QStringLiteral("DISARM:disarmed")),
             qPrintable(events.join(QStringLiteral(", "))));
}

// Which motor table a copter's estimate uses is stored per aircraft (v26):
// linked, replaced, unlinked, and kept apart for each aircraft; with it the
// copter's last hover (v27).
void DatabaseManagerTest::testVehicleMotorTableLink()
{
    QTemporaryDir dir;
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize(dir.filePath(QStringLiteral("mt.db"))));
    QVERIFY(db.schemaVersion() >= 28);
    QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());
    const QString a = QStringLiteral("UID-MT-A"), b = QStringLiteral("UID-MT-B");
    db.linkVehicleMotorTable(a, QString());
    db.linkVehicleMotorTable(b, QString());
    QVERIFY(db.vehicleMotorTableId(a).isEmpty());
    QVERIFY(db.linkVehicleMotorTable(a, QStringLiteral("m6215-320kv-msc22x7.8-6s")));
    QCOMPARE(db.vehicleMotorTableId(a), QStringLiteral("m6215-320kv-msc22x7.8-6s"));
    QVERIFY(db.vehicleMotorTableId(b).isEmpty());
    QVERIFY(db.linkVehicleMotorTable(a, QStringLiteral("other")));
    QCOMPARE(db.vehicleMotorTableId(a), QStringLiteral("other"));
    QVERIFY(db.linkVehicleMotorTable(a, QString()));
    QVERIFY(db.vehicleMotorTableId(a).isEmpty());
    QVERIFY(!db.linkVehicleMotorTable(QString(), QStringLiteral("other")));

    // The last hover (v27) is kept with the link: relinking the same table
    // keeps it, another table forgets it, an unlinked aircraft stores none.
    QVERIFY(!db.saveVehicleMotorHover(a, 3000.0, 20));
    QVERIFY(db.vehicleMotorHover(a).isEmpty());
    QVERIFY(db.linkVehicleMotorTable(a, QStringLiteral("m6215-320kv-msc22x7.8-6s")));
    QVERIFY(db.vehicleMotorHover(a).isEmpty());
    QVERIFY(db.saveVehicleMotorHover(a, 3000.0, 20));
    QCOMPARE(db.vehicleMotorHover(a).value(QStringLiteral("thrustG")).toDouble(), 3000.0);
    QCOMPARE(db.vehicleMotorHover(a).value(QStringLiteral("samples")).toInt(), 20);
    QVERIFY(!db.saveVehicleMotorHover(a, 0.0, 20));
    QVERIFY(db.linkVehicleMotorTable(a, QStringLiteral("m6215-320kv-msc22x7.8-6s")));
    QCOMPARE(db.vehicleMotorHover(a).value(QStringLiteral("thrustG")).toDouble(), 3000.0);
    QVERIFY(db.vehicleMotorHover(b).isEmpty());
    QVERIFY(db.linkVehicleMotorTable(a, QStringLiteral("other")));
    QVERIFY(db.vehicleMotorHover(a).isEmpty());
    QVERIFY(db.linkVehicleMotorTable(a, QString()));

    // Its flights for the charger comparison (v28): recorded, the charger
    // number entered (a pending flight becomes used), dismissed; per aircraft
    // and table, oldest first.
    QVariantMap f{{QStringLiteral("deviceUid"), a}, {QStringLiteral("tableId"), QStringLiteral("m6215-320kv-msc22x7.8-6s")},
                  {QStringLiteral("gcsAh"), 14.2}, {QStringLiteral("startedFull"), true}, {QStringLiteral("startSoc"), 100.0},
                  {QStringLiteral("hoverW"), 1666.0}, {QStringLiteral("hoverS"), 300.0}, {QStringLiteral("avgVolts"), 22.6},
                  {QStringLiteral("minVolts"), 18.9}, {QStringLiteral("beyondS"), 0.0}, {QStringLiteral("density"), 0.75}};
    const int id1 = db.insertMotorTableFlight(f);
    QVERIFY(id1 > 0);
    f[QStringLiteral("gcsAh")] = 9.0;
    f[QStringLiteral("startedFull")] = false;
    const int id2 = db.insertMotorTableFlight(f);
    f[QStringLiteral("tableId")] = QStringLiteral("other");
    QVERIFY(db.insertMotorTableFlight(f) > 0);
    QVariantList rows = db.motorTableFlights(a, QStringLiteral("m6215-320kv-msc22x7.8-6s"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].toMap().value(QStringLiteral("id")).toInt(), id1);
    QCOMPARE(rows[0].toMap().value(QStringLiteral("gcsAh")).toDouble(), 14.2);
    QVERIFY(rows[0].toMap().value(QStringLiteral("startedFull")).toBool());
    QVERIFY(!rows[1].toMap().value(QStringLiteral("startedFull")).toBool());
    QCOMPARE(rows[0].toMap().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    QVERIFY(!rows[0].toMap().value(QStringLiteral("chargerMah")).isValid() || rows[0].toMap().value(QStringLiteral("chargerMah")).isNull());
    QVERIFY(db.setMotorTableFlightCharger(id1, 14600.0));
    QVERIFY(!db.setMotorTableFlightCharger(id1, 0.0));
    QVERIFY(!db.setMotorTableFlightCharger(id1, 300000.0));
    rows = db.motorTableFlights(a, QStringLiteral("m6215-320kv-msc22x7.8-6s"));
    QCOMPARE(rows[0].toMap().value(QStringLiteral("chargerMah")).toDouble(), 14600.0);
    QCOMPARE(rows[0].toMap().value(QStringLiteral("state")).toString(), QStringLiteral("used"));
    QVERIFY(db.setMotorTableFlightState(id2, QStringLiteral("dismissed")));
    QVERIFY(!db.setMotorTableFlightState(id2, QStringLiteral("nonsense")));
    QVERIFY(db.setMotorTableFlightCharger(id2, 9500.0));     // a dismissed flight stays dismissed
    QCOMPARE(db.motorTableFlights(a, QStringLiteral("m6215-320kv-msc22x7.8-6s"))[1].toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("dismissed"));
    QVERIFY(db.motorTableFlights(b, QStringLiteral("m6215-320kv-msc22x7.8-6s")).isEmpty());

    // v29: the mission estimate against the flight, kept with it (JSON);
    // flights without one read back empty.
    QCOMPARE(rows[0].toMap().value(QStringLiteral("missionJson")).toString(), QString());
    f[QStringLiteral("tableId")] = QStringLiteral("m6215-320kv-msc22x7.8-6s");
    f[QStringLiteral("missionJson")] = QStringLiteral("{\"predTotalS\":179,\"realTotalS\":181}");
    const int id3 = db.insertMotorTableFlight(f);
    QVERIFY(id3 > 0);
    rows = db.motorTableFlights(a, QStringLiteral("m6215-320kv-msc22x7.8-6s"));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("id")).toInt(), id3);
    QCOMPARE(rows.last().toMap().value(QStringLiteral("missionJson")).toString(),
             QStringLiteral("{\"predTotalS\":179,\"realTotalS\":181}"));
}

// The first failed write sets a runtime error (separate from startupError,
// which the flight form uses to refuse to start) and notifies once. Further
// failures keep the first message and do not notify again.
void DatabaseManagerTest::testRuntimeWriteErrorOnFirstFailure()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);
    QSignalSpy spy(&db, &DatabaseManager::runtimeErrorChanged);
    QVERIFY(db.runtimeError().isEmpty());

    // Force a write failure through a public write function by removing its
    // table (the default SQLite connection is the one DatabaseManager uses).
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("DROP TABLE flight_telemetry_events")));

    QVERIFY(!db.insertTelemetryEvent(1, QStringLiteral("X"), QString(), 0, 0, 0, QString()));
    QVERIFY(!db.insertTelemetryEvent(1, QStringLiteral("X"), QString(), 0, 0, 0, QString()));
    QVERIFY(!db.runtimeError().isEmpty());
    QVERIFY2(db.runtimeError().contains(QStringLiteral("insertTelemetryEvent")),
             qPrintable(db.runtimeError()));
    QCOMPARE(spy.count(), 1);
    // The form's startup error is untouched: logging did open at start-up.
    QVERIFY(db.startupError().isEmpty());
}

// Known normal-path failures must not raise the banner: the fingerprint
// re-key on a hardware UID (vehicle_config FK) and the read-only helpers.
void DatabaseManagerTest::testRuntimeWriteErrorIgnoresExpectedFailures()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);
    QSignalSpy spy(&db, &DatabaseManager::runtimeErrorChanged);

    // Make updateVehicleFingerprint's UPDATE fail with "no such table".
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys = OFF")));
    QVERIFY(q.exec(QStringLiteral("DROP TABLE vehicles")));
    QVERIFY(!db.updateVehicleFingerprint(QStringLiteral("old"), QStringLiteral("new"),
                                         QStringLiteral("HARDWARE_UID")));
    QVERIFY(db.runtimeError().isEmpty());
    QCOMPARE(spy.count(), 0);
}

// When the hardware UID arrives for a copter registered under the sysid
// fallback, the copter is re-keyed to its decimal UID: every table keyed by it
// and every per-copter setting follows, in one transaction, even once it has
// flown. The compliance log is an audit trail keyed by sysid and stays as is.
void DatabaseManagerTest::testVehicleReKeyMovesDependentsAndSettings()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);

    const QString oldFp = QStringLiteral("rekey-fallback-1111");
    const QString newFp = QStringLiteral("rekey-hardware-2222");
    const QString newUid = QStringLiteral("777000111");
    QVERIFY(db.registerNewVehicle(oldFp, 1, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.5"),
                                  0, QString(), QStringLiteral("board"), QString(),
                                  QStringLiteral("SYSID_TYPE_FALLBACK")));
    QVERIFY(seedVehicleDependents(oldFp, oldFp));
    clearVehicleSettings(oldFp);
    clearVehicleSettings(newUid);
    seedVehicleSettings(oldFp);

    QVERIFY2(db.updateVehicleFingerprint(oldFp, newFp, QStringLiteral("HARDWARE_UID"), newUid),
             "re-key must succeed even when the vehicle has flown");

    const QVariantMap v = row(QStringLiteral(
        "SELECT device_uid, fingerprint_source FROM vehicles WHERE fingerprint='%1'").arg(newFp));
    QVERIFY(!v.isEmpty());
    QCOMPARE(v.value(QStringLiteral("device_uid")).toString(), newUid);
    QCOMPARE(v.value(QStringLiteral("fingerprint_source")).toString(), QStringLiteral("HARDWARE_UID"));
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles")), 1);

    // Every vehicle-keyed table followed the key; nothing is left on the old one.
    const QStringList byUid{QStringLiteral("flight_sessions"), QStringLiteral("check_results"),
                            QStringLiteral("flights"), QStringLiteral("connection_sessions"),
                            QStringLiteral("vehicle_power_type"), QStringLiteral("vehicle_motor_table"),
                            QStringLiteral("motor_table_flights")};
    for (const QString &t : byUid) {
        QVERIFY2(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM %1 WHERE device_uid='%2'").arg(t, newUid)) == 1,
                 qPrintable(t));
        QVERIFY2(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM %1 WHERE device_uid='%2'").arg(t, oldFp)) == 0,
                 qPrintable(t));
    }
    QCOMPARE(countRows(QStringLiteral(
        "SELECT COUNT(*) AS n FROM vehicle_config WHERE fingerprint='%1'").arg(newFp)), 1);
    QCOMPARE(countRows(QStringLiteral(
        "SELECT COUNT(*) AS n FROM compliance_logs WHERE vehicle_id='%1'").arg(oldFp)), 1);
    QVERIFY2(noForeignKeyViolations(), "the re-key left a row pointing at a vehicle that is gone");

    QVERIFY(allVehicleSettingsPresent(newUid));
    QVERIFY(!anyVehicleSettingPresent(oldFp));
    clearVehicleSettings(oldFp);
    clearVehicleSettings(newUid);
}

// From the second connect on, the copter is first registered again under the
// fallback key (the UID arrives a moment after the first heartbeat) and its
// decimal-UID row already exists. The re-key then merges: the fallback's
// rows move onto the existing copter, the existing copter's own setup and
// learning win where both have one, the fallback fills only gaps, and one
// vehicles row is left.
void DatabaseManagerTest::testVehicleReKeyMergesIntoExistingVehicle()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);

    const QString oldFp = QStringLiteral("merge-fallback-1111");
    const QString newFp = QStringLiteral("merge-hardware-2222");
    const quint64 uid = 777000222;
    const QString key = QString::number(uid);
    QVERIFY(db.registerNewVehicle(newFp, 1, 1, QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.6"), uid, QStringLiteral("hw"),
                                  QStringLiteral("board"), QStringLiteral("Real Copter"),
                                  QStringLiteral("HARDWARE_UID")));
    QVERIFY(db.updateVehicleBatteryConfig(key, 6, 8, 5000, 0.2, QStringLiteral("LiIon")));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_motor_table (device_uid, table_id, linked_at, hover_thrust_g, hover_samples) "
                                  "VALUES ('%1', 'tbl-target', '2026-01-01T00:00:00Z', 3000, 40)").arg(key)));
    clearVehicleSettings(key);
    clearVehicleSettings(oldFp);
    QSettings s;
    s.setValue(QStringLiteral("MotorTable/packOhm/%1").arg(key), 0.011);
    s.setValue(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key), true);

    // This connect's fresh fallback profile, with one flight recorded under it
    // and settings written before the UID arrived.
    QVERIFY(db.registerNewVehicle(oldFp, 1, 1, QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.6"), 0, QString(),
                                  QStringLiteral("board"), QString(), QStringLiteral("SYSID_TYPE_FALLBACK")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO flight_sessions (device_uid, started_at) VALUES ('%1', '2026-01-02T00:00:00Z')").arg(oldFp)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_motor_table (device_uid, table_id, linked_at) "
                                  "VALUES ('%1', 'tbl-fallback', '2026-01-02T00:00:00Z')").arg(oldFp)));
    s.setValue(QStringLiteral("MotorTable/packOhm/%1").arg(oldFp), 0.05);
    s.setValue(QStringLiteral("WindFromLean/k/%1").arg(oldFp), 0.42);
    s.sync();

    QVERIFY(db.updateVehicleFingerprint(oldFp, newFp, QStringLiteral("HARDWARE_UID"), key));

    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles")), 1);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles WHERE device_uid='%1'").arg(key)), 1);
    int series = 0, parallel = 0, cellMah = 0; double reserve = 0; QString type;
    QVERIFY(db.vehicleBatteryConfig(key, &series, &parallel, &cellMah, &reserve, &type));
    QCOMPARE(parallel, 8);
    QCOMPARE(type, QStringLiteral("LiIon"));
    const QVariantMap mt = row(QStringLiteral("SELECT table_id, hover_thrust_g FROM vehicle_motor_table WHERE device_uid='%1'").arg(key));
    QCOMPARE(mt.value(QStringLiteral("table_id")).toString(), QStringLiteral("tbl-target"));
    QCOMPARE(mt.value(QStringLiteral("hover_thrust_g")).toDouble(), 3000.0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicle_motor_table WHERE device_uid='%1'").arg(oldFp)), 0);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM flight_sessions WHERE device_uid='%1'").arg(key)), 1);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM flight_sessions WHERE device_uid='%1'").arg(oldFp)), 0);
    QVERIFY2(noForeignKeyViolations(), "the re-key left a row pointing at a vehicle that is gone");

    QSettings after;
    QCOMPARE(after.value(QStringLiteral("MotorTable/packOhm/%1").arg(key)).toDouble(), 0.011);   // the copter's own wins
    QVERIFY(after.value(QStringLiteral("MotorTable/fcCurrentDead/%1").arg(key)).toBool());
    QCOMPARE(after.value(QStringLiteral("WindFromLean/k/%1").arg(key)).toDouble(), 0.42);       // a gap is filled
    QVERIFY(!anyVehicleSettingPresent(oldFp));
    clearVehicleSettings(key);
    clearVehicleSettings(oldFp);
}

// Export carries the copter's setup (pack, weight, sysid, target), import
// keeps it, an older export without those fields leaves them as they are,
// and a bad row rolls the whole import back.
void DatabaseManagerTest::testVehicleImportPreservesPackWeightSysidTargetAndIsAtomic()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);

    const QString fp = QStringLiteral("import-fp-4444");
    const int sysid = 7;
    const quint64 uid = 777001;
    const QString key = QString::number(uid);
    QVERIFY(db.registerNewVehicle(fp, sysid, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.5"),
                                  uid, QStringLiteral("hw"), QStringLiteral("board"),
                                  QStringLiteral("Import Me"), QStringLiteral("HARDWARE_UID")));
    QVERIFY(db.updateVehicleUavWeight(key, 13.5));
    QVERIFY(db.updateVehicleBatteryConfig(key, 6, 8, 5000, 0.25, QStringLiteral("Li-ion")));
    QVERIFY(db.saveVehicleTarget(key, 47.5, 8.5, QStringLiteral("test-import")));

    const QJsonDocument doc = QJsonDocument::fromJson(db.exportVehiclesJson().toUtf8());
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 1);
    const QJsonObject o = doc.array().first().toObject();
    QCOMPARE(o.value(QStringLiteral("deviceUid")).toString(), key);
    QCOMPARE(o.value(QStringLiteral("uavWeightKg")).toDouble(), 13.5);
    QCOMPARE(o.value(QStringLiteral("batterySeriesCells")).toInt(), 6);
    QCOMPARE(o.value(QStringLiteral("batteryParallelCells")).toInt(), 8);
    QCOMPARE(o.value(QStringLiteral("batteryCellMah")).toInt(), 5000);
    QCOMPARE(o.value(QStringLiteral("batteryReserve")).toDouble(), 0.25);
    QCOMPARE(o.value(QStringLiteral("batteryType")).toString(), QStringLiteral("Li-ion"));
    QCOMPARE(o.value(QStringLiteral("sysid")).toInt(), 7);
    QCOMPARE(o.value(QStringLiteral("targetLat")).toDouble(), 47.5);
    QCOMPARE(o.value(QStringLiteral("targetLon")).toDouble(), 8.5);
    QCOMPARE(o.value(QStringLiteral("targetSource")).toString(), QStringLiteral("test-import"));

    const char *setupSql =
        "SELECT uav_weight_kg, battery_series_cells, battery_parallel_cells, battery_cell_mah, "
        "battery_reserve, battery_type, sysid, target_lat, target_lon, target_source "
        "FROM vehicles WHERE device_uid='%1'";
    auto checkSetup = [&]() {
        const QVariantMap v = row(QString::fromLatin1(setupSql).arg(key));
        QVERIFY(!v.isEmpty());
        QCOMPARE(v.value(QStringLiteral("uav_weight_kg")).toDouble(), 13.5);
        QCOMPARE(v.value(QStringLiteral("battery_series_cells")).toInt(), 6);
        QCOMPARE(v.value(QStringLiteral("battery_parallel_cells")).toInt(), 8);
        QCOMPARE(v.value(QStringLiteral("battery_cell_mah")).toInt(), 5000);
        QCOMPARE(v.value(QStringLiteral("battery_reserve")).toDouble(), 0.25);
        QCOMPARE(v.value(QStringLiteral("battery_type")).toString(), QStringLiteral("Li-ion"));
        QCOMPARE(v.value(QStringLiteral("sysid")).toInt(), 7);
        QCOMPARE(v.value(QStringLiteral("target_lat")).toDouble(), 47.5);
        QCOMPARE(v.value(QStringLiteral("target_lon")).toDouble(), 8.5);
        QCOMPARE(v.value(QStringLiteral("target_source")).toString(), QStringLiteral("test-import"));
    };

    // An older export (no setup fields) imported over the configured copter
    // leaves its setup alone.
    QJsonObject old = o;
    for (const char *k : {"uavWeightKg", "batterySeriesCells", "batteryParallelCells", "batteryCellMah",
                          "batteryReserve", "batteryType", "sysid", "targetLat", "targetLon", "targetSource",
                          "fingerprintSource", "hardwareUid"})
        old.remove(QLatin1String(k));
    old[QStringLiteral("friendlyName")] = QStringLiteral("Renamed On Import");
    QVERIFY(db.importVehiclesJson(QString::fromUtf8(QJsonDocument(QJsonArray{old}).toJson(QJsonDocument::Compact))));
    checkSetup();
    QCOMPARE(row(QStringLiteral("SELECT friendly_name FROM vehicles WHERE device_uid='%1'").arg(key))
                 .value(QStringLiteral("friendly_name")).toString(), QStringLiteral("Renamed On Import"));

    // Round-trip into an empty vehicles table.
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("DELETE FROM vehicles")));
    QVERIFY(db.importVehiclesJson(QString::fromUtf8(QJsonDocument(doc).toJson(QJsonDocument::Compact))));
    checkSetup();

    // A bad row must roll the good one back; a partial import is not useful.
    QVERIFY(q.exec(QStringLiteral("DELETE FROM vehicles")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TRIGGER import_fail_test BEFORE INSERT ON vehicles "
        "WHEN NEW.device_uid = 'import-bad-uid' "
        "BEGIN SELECT RAISE(ABORT, 'forced import failure'); END")));
    QJsonArray arr;
    arr.append(o);
    QJsonObject bad = o;
    bad[QStringLiteral("deviceUid")] = QStringLiteral("import-bad-uid");
    bad[QStringLiteral("fingerprint")] = QStringLiteral("import-bad-fp");
    arr.append(bad);
    QVERIFY(!db.importVehiclesJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))));
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles")), 0);
    QVERIFY(q.exec(QStringLiteral("DROP TRIGGER import_fail_test")));
}

// A copter with recorded history (flights, checks, connections) is never
// deleted: the call is refused with a reason and nothing changes.
void DatabaseManagerTest::testVehicleDeleteRefusedWhileItHasHistory()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);

    const QString fp = QStringLiteral("delete-fp-3333");
    const quint64 uid = 888888;
    const QString key = QString::number(uid);
    QVERIFY(db.registerNewVehicle(fp, 3, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.5"),
                                  uid, QStringLiteral("hw"), QStringLiteral("board"),
                                  QStringLiteral("Delete Me"), QStringLiteral("HARDWARE_UID")));
    QVERIFY(seedVehicleDependents(key, fp));
    clearVehicleSettings(key);
    seedVehicleSettings(key);

    QVERIFY(!db.deleteVehicle(key));
    QVERIFY2(db.vehicleDeleteError().contains(QStringLiteral("history")), qPrintable(db.vehicleDeleteError()));

    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles WHERE device_uid='%1'").arg(key)), 1);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM flight_sessions WHERE device_uid='%1'").arg(key)), 1);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicle_motor_table WHERE device_uid='%1'").arg(key)), 1);
    QVERIFY(allVehicleSettingsPresent(key));
    clearVehicleSettings(key);

    // History with no foreign key behind it (only connections recorded) is
    // history too: still refused, the copter and its table link kept.
    const QString fp2 = QStringLiteral("delete-fp-3334");
    const QString key2 = QStringLiteral("888889");
    QVERIFY(db.registerNewVehicle(fp2, 4, 1, QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.5"), 888889, QStringLiteral("hw"),
                                  QStringLiteral("board"), QStringLiteral("Connected Once"), QStringLiteral("HARDWARE_UID")));
    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO connection_sessions (device_uid, opened_at) VALUES ('%1', '2026-01-01T00:00:00Z')").arg(key2)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES ('%1', 't1', '2026-01-01T00:00:00Z')").arg(key2)));
    QVERIFY(!db.deleteVehicle(key2));
    QVERIFY2(db.vehicleDeleteError().contains(QStringLiteral("history")), qPrintable(db.vehicleDeleteError()));
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles WHERE device_uid='%1'").arg(key2)), 1);
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicle_motor_table WHERE device_uid='%1'").arg(key2)), 1);
}

// A copter with no recorded history is deleted with its setup: its table
// link, learning, power type, config and per-copter settings go with it.
void DatabaseManagerTest::testVehicleDeleteRemovesSetupWhenNoHistory()
{
    DatabaseManager &db = DatabaseManager::instance();
    initTempDb(db);

    const QString fp = QStringLiteral("delete-fp-5555");
    const quint64 uid = 888999;
    const QString key = QString::number(uid);
    QVERIFY(db.registerNewVehicle(fp, 3, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("multirotor"),
                                  QStringLiteral("Quad"), QStringLiteral("4.5"),
                                  uid, QStringLiteral("hw"), QStringLiteral("board"),
                                  QStringLiteral("Delete Me Too"), QStringLiteral("HARDWARE_UID")));
    QSqlQuery q(QSqlDatabase::database());
    const QString ts = QStringLiteral("2026-01-01T00:00:00Z");
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_config (fingerprint, config_json) VALUES ('%1', '{}')").arg(fp)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO power_types (frame, prop_diameter_in, prop_pitch_in, cells, created_at) VALUES ('quad', 10, 5, 6, '%1')").arg(ts)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_power_type (device_uid, power_type_id, linked_at) SELECT '%1', id, '%2' FROM power_types LIMIT 1").arg(key, ts)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES ('%1', 't1', '%2')").arg(key, ts)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO motor_table_flights (device_uid, table_id, started_at, ended_at, gcs_ah) VALUES ('%1', 't1', '%2', '%2', 1.0)").arg(key, ts)));
    clearVehicleSettings(key);
    seedVehicleSettings(key);

    QVERIFY2(db.deleteVehicle(key), qPrintable(db.vehicleDeleteError()));

    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicles WHERE device_uid='%1'").arg(key)), 0);
    for (const QString &t : {QStringLiteral("vehicle_power_type"), QStringLiteral("vehicle_motor_table"),
                             QStringLiteral("motor_table_flights")})
        QVERIFY2(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM %1 WHERE device_uid='%2'").arg(t, key)) == 0,
                 qPrintable(t));
    QCOMPARE(countRows(QStringLiteral("SELECT COUNT(*) AS n FROM vehicle_config WHERE fingerprint='%1'").arg(fp)), 0);
    QVERIFY(!anyVehicleSettingPresent(key));
    clearVehicleSettings(key);
}

// Each migration step takes a snapshot named after the version it starts
// from. A retry of the same step must keep the first snapshot, not overwrite
// it with the half-migrated file: that copy is the only way back if a later
// attempt fails part-way through.
void DatabaseManagerTest::testMigrationRetryKeepsOriginalBackup()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryFile tmpFile(QDir::tempPath() + QStringLiteral("/test_migration_retry_XXXXXX.db"));
    QVERIFY(tmpFile.open());
    tmpFile.close();
    const QString path = tmpFile.fileName();
    const QString v0bak = path + QStringLiteral(".v0.bak");
    const int previousVersion = db.schemaVersion() - 1;
    const QString prevBak = path + QStringLiteral(".v%1.bak").arg(previousVersion);

    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(v0bak));

    // Pretend a migration stopped one version back: the next initialize()
    // re-runs the last step and takes a snapshot of that file.
    {
        QSqlQuery q(QSqlDatabase::database());
        QVERIFY(q.exec(QStringLiteral("DELETE FROM schema_version WHERE version > %1").arg(previousVersion)));
    }
    db.reset();
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(prevBak));
    QFile original(prevBak);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray snapshot = original.readAll();
    original.close();
    QVERIFY(!snapshot.isEmpty());

    // New writes, then the same step is retried from the same version. The
    // snapshot from the first attempt must survive byte for byte.
    QVERIFY(db.insertOperator(QStringLiteral("After Backup"), QStringLiteral("pilot")) > 0);
    {
        QSqlQuery q(QSqlDatabase::database());
        QVERIFY(q.exec(QStringLiteral("DELETE FROM schema_version WHERE version > %1").arg(previousVersion)));
    }
    db.reset();
    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(prevBak));
    QFile retried(prevBak);
    QVERIFY(retried.open(QIODevice::ReadOnly));
    QCOMPARE(retried.readAll(), snapshot);
    retried.close();
    QVERIFY(QFile::exists(v0bak));

    QFile::remove(path);
    QFile::remove(v0bak);
    QFile::remove(prevBak);
}

// A fresh database keeps its v0 snapshot; upgrading later from another
// version takes a second snapshot under that version's name and leaves the
// first one untouched. Version 0 must still be backed up: a database written
// before schema versioning also reads as 0.
void DatabaseManagerTest::testLaterUpgradeMakesItsOwnBackup()
{
    DatabaseManager &db = DatabaseManager::instance();
    QTemporaryFile tmpFile(QDir::tempPath() + QStringLiteral("/test_later_backup_XXXXXX.db"));
    QVERIFY(tmpFile.open());
    tmpFile.close();
    const QString path = tmpFile.fileName();
    const QString v0bak = path + QStringLiteral(".v0.bak");
    const int previousVersion = db.schemaVersion() - 1;
    const QString prevBak = path + QStringLiteral(".v%1.bak").arg(previousVersion);

    QVERIFY(db.initialize(path));
    QVERIFY(QFile::exists(v0bak));
    QFile first(v0bak);
    QVERIFY(first.open(QIODevice::ReadOnly));
    const QByteArray snapshot = first.readAll();
    first.close();
    QVERIFY(!snapshot.isEmpty());

    {
        QSqlQuery q(QSqlDatabase::database());
        QVERIFY(q.exec(QStringLiteral("DELETE FROM schema_version WHERE version > %1").arg(previousVersion)));
    }
    db.reset();
    QVERIFY(db.initialize(path));

    QVERIFY(QFile::exists(prevBak));
    QFile original(v0bak);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), snapshot);
    original.close();

    QFile::remove(path);
    QFile::remove(v0bak);
    QFile::remove(prevBak);
}

UT_REGISTER_TEST(DatabaseManagerTest)

#include "DatabaseManagerTest.moc"
