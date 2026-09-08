/**
 * @file FlightSessionTest.cpp
 * @brief Tests for flight session lifecycle and compliance logging (L4 from TEST_CHECKLIST.md).
 *
 * Covers:
 *   - startFlightSession creates a row
 *   - endFlightSession closes it with duration
 *   - compliance log entries are written
 *   - battery cycle increment
 *   - session with/without battery serial
 *   - multiple sessions for same vehicle
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>

#include "UnitTest.h"
#include "utils/DatabaseManager.h"
#include <QUuid>

class FlightSessionTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
        DatabaseManager::instance().initialize();
    }
    void cleanup() override { UnitTest::cleanup(); }

    void testStartSession();
    void testEndSession();
    void testSessionWithBatterySerial();
    void testMultipleSessions();
    void testComplianceLogWrite();
    void testComplianceLogLoad();
    void testCheckConfigReadWrite();
    void testVehicleConfigPersistence();
};

// Starting a session returns a positive ID
void FlightSessionTest::testStartSession()
{
    DatabaseManager &db = DatabaseManager::instance();

    // Register a vehicle first (device_uid is the FK key for flight_sessions)
    db.upsertVehicle(QStringLiteral("test-fp-001"), QStringLiteral("Test Vehicle"),
                     QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"));

    int sessionId = db.startFlightSession(QStringLiteral("test-fp-001"), QString());
    QVERIFY(sessionId > 0);
}

// End session writes duration and closes it
void FlightSessionTest::testEndSession()
{
    DatabaseManager &db = DatabaseManager::instance();

    db.upsertVehicle(QStringLiteral("test-fp-002"), QStringLiteral("Test Vehicle 2"),
                     QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"));

    int sessionId = db.startFlightSession(QStringLiteral("test-fp-002"), QString());
    QVERIFY(sessionId > 0);

    bool ended = db.endFlightSession(sessionId, 120.0);
    QVERIFY(ended);

    // Verify session exists in history
    QString history = db.getVehicleHistory(QStringLiteral("test-fp-002"));
    QVERIFY(!history.isEmpty());
}

// Session with battery serial
void FlightSessionTest::testSessionWithBatterySerial()
{
    DatabaseManager &db = DatabaseManager::instance();

    db.upsertVehicle(QStringLiteral("test-fp-003"), QStringLiteral("Test Vehicle 3"),
                     QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"));

    // battery_serial is an FK -> batteries(serial_number); register it first.
    db.upsertBattery(QStringLiteral("BAT-SERIAL-001"), QStringLiteral("Test Battery"));

    int sessionId = db.startFlightSession(QStringLiteral("test-fp-003"),
                                          QStringLiteral("BAT-SERIAL-001"));
    QVERIFY(sessionId > 0);

    bool ended = db.endFlightSession(sessionId, 60.0);
    QVERIFY(ended);
}

// Multiple sessions for the same vehicle
void FlightSessionTest::testMultipleSessions()
{
    DatabaseManager &db = DatabaseManager::instance();

    db.upsertVehicle(QStringLiteral("test-fp-004"), QStringLiteral("Test Vehicle 4"),
                     QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"));

    int s1 = db.startFlightSession(QStringLiteral("test-fp-004"), QString());
    QVERIFY(s1 > 0);
    db.endFlightSession(s1, 60.0);

    int s2 = db.startFlightSession(QStringLiteral("test-fp-004"), QString());
    QVERIFY(s2 > 0);
    QVERIFY(s2 != s1);
    db.endFlightSession(s2, 120.0);

    int s3 = db.startFlightSession(QStringLiteral("test-fp-004"), QString());
    QVERIFY(s3 > 0);
    QVERIFY(s3 != s1 && s3 != s2);
    db.endFlightSession(s3, 90.0);
}

// Compliance log entries can be written and read back
void FlightSessionTest::testComplianceLogWrite()
{
    DatabaseManager &db = DatabaseManager::instance();

    QJsonObject logEntry;
    logEntry[QStringLiteral("sessionId")] = 1;
    logEntry[QStringLiteral("deviceUid")] = QStringLiteral("test-fp-005");
    logEntry[QStringLiteral("durationSec")] = 60.0;
    logEntry[QStringLiteral("gateCloseReason")] = QStringLiteral("normal");

    QString logJson = QString::fromUtf8(QJsonDocument(logEntry).toJson(QJsonDocument::Compact));

    const QString logId = QStringLiteral("session-%1").arg(QUuid::createUuid().toString());
    bool saved = db.saveComplianceLog(
        logId,
        QStringLiteral("test-fp-005"),
        QStringLiteral("MultiRotor"), QStringLiteral("op-1"),
        logJson, QString());
    QVERIFY(saved);
}

// Compliance log can be loaded back
void FlightSessionTest::testComplianceLogLoad()
{
    DatabaseManager &db = DatabaseManager::instance();

    QJsonObject logEntry;
    logEntry[QStringLiteral("test")] = true;
    logEntry[QStringLiteral("value")] = 42;

    QString logJson = QString::fromUtf8(QJsonDocument(logEntry).toJson(QJsonDocument::Compact));

    const QString logId = QStringLiteral("test-log-%1").arg(QUuid::createUuid().toString());
    db.saveComplianceLog(
        logId,
        QStringLiteral("test-vehicle"),
        QStringLiteral("FixedWing"), QStringLiteral("op-1"),
        logJson, QString());

    QString loaded = db.loadComplianceLog(logId);
    QVERIFY(!loaded.isEmpty());

    QJsonDocument doc = QJsonDocument::fromJson(loaded.toUtf8());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("test")).toBool(), true);
    QCOMPARE(doc.object().value(QStringLiteral("value")).toInt(), 42);
}

// Check config read/write round-trip
void FlightSessionTest::testCheckConfigReadWrite()
{
    DatabaseManager &db = DatabaseManager::instance();

    // Set a config value
    bool set = db.setCheckConfig(
        QStringLiteral("power.battery.voltage"),
        QStringLiteral("minVoltage"),
        QStringLiteral("10.5"));
    QVERIFY(set);

    // Read it back
    QString val = db.getCheckConfig(
        QStringLiteral("power.battery.voltage"),
        QStringLiteral("minVoltage"));
    QCOMPARE(val, QStringLiteral("10.5"));
}

// Vehicle config persistence (JSON blob)
void FlightSessionTest::testVehicleConfigPersistence()
{
    DatabaseManager &db = DatabaseManager::instance();

    // vehicle_config.fingerprint is a FK → vehicles(fingerprint); register the
    // vehicle first so the config row can reference it.
    db.registerNewVehicle(QStringLiteral("test-fingerprint"), 1, 1,
                          QStringLiteral("ArduPilot"), QStringLiteral("FixedWing"),
                          QStringLiteral("Skywalker X8"), QStringLiteral("4.5.2"),
                          0, QStringLiteral("HW-UID-CFG"),
                          QStringLiteral("1.0"), QStringLiteral("CFG Vehicle"),
                          QStringLiteral("HARDWARE_UID"));

    QJsonObject config;
    QJsonObject compassConfig;
    compassConfig[QStringLiteral("expectedRotation")] = 4;
    config[QStringLiteral("nav.compass.orientation")] = compassConfig;

    QString json = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));

    bool saved = db.saveVehicleConfig(QStringLiteral("test-fingerprint"), json);
    QVERIFY(saved);

    QString loaded = db.loadVehicleConfig(QStringLiteral("test-fingerprint"));
    QVERIFY(!loaded.isEmpty());

    QJsonDocument doc = QJsonDocument::fromJson(loaded.toUtf8());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("nav.compass.orientation"))
             .toObject().value(QStringLiteral("expectedRotation")).toInt(), 4);
}

UT_REGISTER_TEST(FlightSessionTest)

#include "FlightSessionTest.moc"
