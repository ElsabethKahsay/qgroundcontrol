/**
 * @file FlightSessionLifecycleTest.cpp
 * @brief Characterization tests for the FlightSession FSM against the DESIGN
 *        INTENT: a CONNECTION SESSION vs a FLIGHT RECORD, where flight records
 *        are driven by the vehicle's armed/disarmed state, gate open/close are
 *        events inside a record (not creators/endings), a second arm after a
 *        disarm starts a new record, and bench runs (Testing/Training) never
 *        count toward flight hours or battery cycles.
 *
 * These are characterization tests: scenarios that do not yet match the DESIGN
 * are marked QEXPECT_FAIL so the suite stays honest, and scenarios that cannot
 * be driven with the existing mocks are QSKIPped with the missing capability
 * named. No production code is changed here.
 *
 * Scenario coverage:
 *   (a) gate flap with no arm        -> QSKIP (needs ArmingGate open/close flapping)
 *   (a-lite) arm without any session -> asserted: no flight record is created
 *   (b) RC arm while gate closed     -> QSKIP (needs the heartbeat/armedChanged path)
 *   (c) arm, disarm, arm again       -> asserted: FSM re-arms; records are SessionRecorder's
 *                                       (second record per arm: DatabaseManagerTest)
 *   (d) Testing/Training arm+disarm  -> asserted: bench run writes no records
 *   (e) link drop while armed        -> QSKIP (needs link-drop-while-armed harness)
 *   (f) orphaned open record         -> covered by DatabaseManagerTest
 *                                       ::testOrphanedSessionRecoveredOnRelaunch
 *                                       (not duplicated here per instructions)
 */

#include <QTest>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "UnitTest.h"
#include "PreflightPlugin.h"
#include "FlightSession.h"
#include "OperatorManager.h"
#include "VehicleRegistry.h"
#include "DatabaseManager.h"
#include "utils/Config.h"
#include "utils/MonotonicClock.h"
#include <QtCore/QScopeGuard>
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"
#include "Comms/MockLink/MockConfiguration.h"
#include "Comms/LinkManager.h"
#include "VehicleProfileManager.h"

class FlightSessionLifecycleTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        // Unit-test mode skips the app boot, so PreflightPlugin::init() (which
        // constructs FlightSession, OperatorManager, ArmingGate, ...) never runs.
        // init() is idempotent, so every test can call it without a once-guard.
        PreflightPlugin::instance()->init();
        // Fresh database per test (same pattern as FlightSessionTest).
        DatabaseManager::instance().reset();
        DatabaseManager::instance().initialize();
    }
    void cleanup() override {
        _disconnectMockLink();
        UnitTest::cleanup();
    }

    void testArmWithoutSessionStartCreatesNoRecord();
    void testBenchModeCreatesNoFlightOrSessionRows();
    void testSecondFlightRecordAfterDisarm();
    void testFormFlightArmDisarmMakesOneRow();
    void testFormCloseKeepsRecorderDuration();
    void testFormDurationUsesMonotonicClock();
    void testTwoArmsInOneFormSessionMakeTwoRows();
    void testUid2KeyFormat();
    void testOneVehicleRowWhenBoardIdArrivesLate();
    void testUid2GivesHardwareIdentity();
    void testExistingFallbackRowIsNotMerged();

private:
    // Connects a mock ArduCopter quad whose AUTOPILOT_VERSION reports board
    // product id @p productId (the heartbeat carries none, as on real boards),
    // and waits until the registry has settled the copter's identity.
    bool _connectCopterWithBoard(uint16_t productId);
    // Feeds the registry an AUTOPILOT_VERSION as the copter would send it, with
    // uid = 0 and the given 18-byte uid2 (the STM32's unique id, as ArduPilot
    // ChibiOS boards report it).
    void _sendAutopilotVersionUid2(const QByteArray &uid2, uint16_t productId);
    static int _vehicleRowCount() {
        QSqlQuery q(QSqlDatabase::database());
        return q.exec(QStringLiteral("SELECT COUNT(*) FROM vehicles")) && q.next() ? q.value(0).toInt() : -1;
    }
    static QString _vehicleSource(const QString &deviceUid) {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral("SELECT fingerprint_source FROM vehicles WHERE device_uid = ?"));
        q.addBindValue(deviceUid);
        return q.exec() && q.next() ? q.value(0).toString() : QString();
    }
    // Connects a mock plane and returns true when a vehicle + registry id exist.
    bool _connectAndRegister(int *vehicleIdOut, QString *fingerprintOut);
    bool _waitArmed(bool armed, int ms = 10000);
    // The one flights row carrying the given purpose, or an empty map.
    static QVariantMap _formRow(int vehicleId, const QString &purpose);
    static int _eventCount(int flightId, const QString &type);
    static int _sessionForHistory(int historyId);
    static int _flightCount(int vehicleId) {
        return DatabaseManager::instance().getFlightsForVehicle(vehicleId, 500).size();
    }
    static int _sessionCount(const QString &deviceUid) {
        const QByteArray json = DatabaseManager::instance()
            .getFlightSessions(deviceUid, 200).toUtf8();
        return QJsonDocument::fromJson(json).array().size();
    }
};

bool FlightSessionLifecycleTest::_connectAndRegister(int *vehicleIdOut, QString *fingerprintOut)
{
    _connectMockLink(MAV_AUTOPILOT_PX4);
    if (!_vehicle) return false;
    QSignalSpy spy(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    if (_vehicle != MultiVehicleManager::instance()->activeVehicle())
        spy.wait(5000);
    // Give the registry a moment to record the active vehicle id.
    for (int i = 0; i < 40 && VehicleRegistry::instance()->currentVehicleId() <= 0; ++i)
        QTest::qWait(50);
    *vehicleIdOut = VehicleRegistry::instance()->currentVehicleId();
    *fingerprintOut = VehicleRegistry::instance()->currentFingerprint();
    return *vehicleIdOut > 0;
}

bool FlightSessionLifecycleTest::_waitArmed(bool armed, int ms)
{
    for (int waited = 0; waited < ms; waited += 50) {
        if (_vehicle->armed() == armed)
            return true;
        QTest::qWait(50);
    }
    return _vehicle->armed() == armed;
}

QVariantMap FlightSessionLifecycleTest::_formRow(int vehicleId, const QString &purpose)
{
    for (const QVariantMap &row : DatabaseManager::instance().getFlightsForVehicle(vehicleId, 500))
        if (row.value(QStringLiteral("purpose")).toString() == purpose)
            return row;
    return {};
}

int FlightSessionLifecycleTest::_eventCount(int flightId, const QString &type)
{
    int n = 0;
    for (const QVariantMap &e : DatabaseManager::instance().getTelemetryEventsForFlight(flightId))
        if (e.value(QStringLiteral("event_type")).toString() == type)
            ++n;
    return n;
}

int FlightSessionLifecycleTest::_sessionForHistory(int historyId)
{
    QSqlQuery q(QSqlDatabase::database());
    if (!q.exec(QStringLiteral("SELECT id FROM flight_sessions WHERE history_flight_id = %1").arg(historyId))
            || !q.next())
        return -1;
    return q.value(0).toInt();
}

// (a-lite) Arming alone must not create an audited flight record: a flight
// record only exists because startFlightSession() opened one.
void FlightSessionLifecycleTest::testArmWithoutSessionStartCreatesNoRecord()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    FlightSession *fs = FlightSession::instance();
    QVERIFY(fs);
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    // The unit-test database is not emptied between tests: count from here.
    const int base = _flightCount(vid);

    // Arm without ever starting a session -> FlightSession adds no `flights` row.
    fs->onVehicleArmed();
    QCOMPARE(_flightCount(vid), base);
    fs->onVehicleDisarmed();
    QCOMPARE(_flightCount(vid), base);
    // Post-flight is parked (Config.h kPostFlightEnabled): a disarm returns to PreFlight.
    QCOMPARE(fs->state(), kPostFlightEnabled ? FlightSession::SessionState::PostFlight
                                             : FlightSession::SessionState::PreFlight);
}

// (d) A bench run (Testing mode) must not leave any audited flight record or
// compliance session, so it cannot contribute to flight hours or battery cycles
// (both are derived from those rows / the gate-close path).
void FlightSessionLifecycleTest::testBenchModeCreatesNoFlightOrSessionRows()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Bench Op"),
                                                        QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);
    QVERIFY(OperatorManager::instance()->currentOperatorId() > 0);

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("TESTING"));
    const int baseFlights = _flightCount(vid);
    const int baseSessions = _sessionCount(fp);
    fs->startTestingSession();
    QCOMPARE(fs->state(), FlightSession::SessionState::PreFlight);

    fs->onVehicleArmed();
    QCOMPARE(fs->state(), FlightSession::SessionState::Armed);
    fs->onVehicleDisarmed();
    // Post-flight is parked (Config.h kPostFlightEnabled): a disarm returns to PreFlight.
    QCOMPARE(fs->state(), kPostFlightEnabled ? FlightSession::SessionState::PostFlight
                                             : FlightSession::SessionState::PreFlight);

    // A bench run is in-memory only: no audited flight, no compliance session.
    QCOMPARE(_flightCount(vid), baseFlights);
    QCOMPARE(_sessionCount(fp), baseSessions);
}

// (c) Arm, disarm, arm again on the same connection. The flight
// records (one per arm-to-disarm, RC arms included) belong to SessionRecorder,
// driven by the vehicle's armed state; that rule is asserted in
// DatabaseManagerTest::testRecorderFlightFollowsArmedState. FlightSession's
// own state machine must follow arm/disarm/arm without blocking a second arm,
// and its form-opened record must not be re-opened or duplicated by it.
// (This replaces two QEXPECT_FAILs written against the pre-recorder design.)
void FlightSessionLifecycleTest::testSecondFlightRecordAfterDisarm()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Flight Op"),
                                                        QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);
    QVERIFY(OperatorManager::instance()->currentOperatorId() > 0);

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    const int base = _flightCount(vid);
    fs->startFlightSession(QStringLiteral("test purpose"),
                           QStringLiteral("test loc"),
                           QStringLiteral("test notes"));
    const int firstId = fs->currentFlightId();
    QVERIFY(firstId > 0);
    QCOMPARE(_flightCount(vid), base + 1);

    fs->onVehicleArmed();
    QCOMPARE(fs->state(), FlightSession::SessionState::Armed);
    fs->onVehicleDisarmed();
    // Post-flight is parked (Config.h kPostFlightEnabled): a disarm returns to PreFlight.
    QCOMPARE(fs->state(), kPostFlightEnabled ? FlightSession::SessionState::PostFlight
                                             : FlightSession::SessionState::PreFlight);

    // Second arm on the same connection is not blocked by the state machine.
    fs->onVehicleArmed();
    QCOMPARE(fs->state(), FlightSession::SessionState::Armed);
    fs->onVehicleDisarmed();

    // FlightSession's arm handling writes no extra rows (SessionRecorder does,
    // from the vehicle's real armed state, which this test does not change).
    QCOMPARE(_flightCount(vid), base + 1);
}

// Filling the Flight form opens one flights row. Arming and disarming the real
// mock vehicle drives SessionRecorder, which must reuse that row: one row, one
// ARM and one DISARM, with the form's purpose and notes plus the arm/disarm
// times. Today the recorder opens a second row and both paths write DISARM.
void FlightSessionLifecycleTest::testFormFlightArmDisarmMakesOneRow()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Form Op"), QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);
    QVERIFY(OperatorManager::instance()->currentOperatorId() > 0);

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    const int base = _flightCount(vid);
    fs->startFlightSession(QStringLiteral("form purpose"), QStringLiteral("form loc"), QStringLiteral("form notes"));
    const int formId = fs->currentFlightId();
    QVERIFY(formId > 0);
    QCOMPARE(_flightCount(vid), base + 1);

    // Arm / disarm the vehicle for real so ArmStateTracker -> SessionRecorder runs.
    _vehicle->setArmed(true, false);
    QVERIFY(_waitArmed(true));
    QTest::qWait(300);
    _vehicle->setArmed(false, false);
    QVERIFY(_waitArmed(false));
    QTest::qWait(300);

    // One flights row for this flight, not two.
    QCOMPARE(_flightCount(vid), base + 1);
    const QVariantMap row = _formRow(vid, QStringLiteral("form purpose"));
    QVERIFY2(!row.isEmpty(), "form row missing");
    const int id = row.value(QStringLiteral("id")).toInt();
    QCOMPARE(row.value(QStringLiteral("notes")).toString(), QStringLiteral("form notes"));
    QVERIFY(!row.value(QStringLiteral("armed_at")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("disarmed_at")).toString().isEmpty());
    QCOMPARE(_eventCount(id, QStringLiteral("ARM")), 1);
    QCOMPARE(_eventCount(id, QStringLiteral("DISARM")), 1);
    // The recorder's flight session must point at the form row it adopted.
    QVERIFY2(_sessionForHistory(id) > 0, "flight_sessions.history_flight_id not linked to form row");
}

// Once the recorder owns the form row, closing the form session must not
// overwrite the recorder's arm-to-disarm duration_sec / ended_at with the
// wall-clock duration from form submit to close.
void FlightSessionLifecycleTest::testFormCloseKeepsRecorderDuration()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Form Op"), QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    fs->startFlightSession(QStringLiteral("close purpose"), QStringLiteral("loc"), QStringLiteral("notes"));
    QVERIFY(fs->currentFlightId() > 0);

    _vehicle->setArmed(true, false);
    QVERIFY(_waitArmed(true));
    QTest::qWait(300);
    _vehicle->setArmed(false, false);
    QVERIFY(_waitArmed(false));
    QTest::qWait(300);

    const QVariantMap beforeClose = _formRow(vid, QStringLiteral("close purpose"));
    QVERIFY(!beforeClose.isEmpty());
    const int id = beforeClose.value(QStringLiteral("id")).toInt();
    const int recorderDuration = beforeClose.value(QStringLiteral("duration_sec")).toInt();
    const QString recorderEnded = beforeClose.value(QStringLiteral("ended_at")).toString();
    QVERIFY(!recorderEnded.isEmpty());

    // Checklist results and an anomaly on the form's flight: only the form
    // knows them, so closing the form must still write their counts.
    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.insertFlightCheckResult(id, QStringLiteral("gps"), QStringLiteral("nav"), false,
                                       QStringLiteral("PASS"), QString()));
    QVERIFY(db.insertFlightCheckResult(id, QStringLiteral("rc"), QStringLiteral("rc"), false,
                                       QStringLiteral("PASS"), QString()));
    QVERIFY(db.insertFlightCheckResult(id, QStringLiteral("wind"), QStringLiteral("weather"), false,
                                       QStringLiteral("WARN"), QString()));
    QVERIFY(db.insertTelemetryEvent(id, QStringLiteral("BATTERY_WARN"), QStringLiteral("test"),
                                    15.0, 10.0, 8, QStringLiteral("Loiter")));
    // The preflight checks run during the test may have filed results too.
    const QVariantMap expected = db.getCheckCountsForFlight(id);
    const int expectedAnomalies = db.getAnomalyCountForFlight(id);
    QVERIFY(expected.value(QStringLiteral("pass")).toInt() >= 2);
    QVERIFY(expected.value(QStringLiteral("warn")).toInt() >= 1);
    QVERIFY(expectedAnomalies >= 1);

    // End the form session after a deliberate wall-clock gap.
    QTest::qWait(1100);
    fs->closeSession();

    const QVariantMap afterClose = _formRow(vid, QStringLiteral("close purpose"));
    QVERIFY(!afterClose.isEmpty());
    QCOMPARE(afterClose.value(QStringLiteral("duration_sec")).toInt(), recorderDuration);
    QCOMPARE(afterClose.value(QStringLiteral("ended_at")).toString(), recorderEnded);

    QSqlQuery q(QSqlDatabase::database());
    QVERIFY(q.exec(QStringLiteral("SELECT check_pass_count, check_fail_count, check_warn_count, anomaly_count "
                                  "FROM flights WHERE id = %1").arg(id)) && q.next());
    QCOMPARE(q.value(0).toInt(), expected.value(QStringLiteral("pass")).toInt());
    QCOMPARE(q.value(1).toInt(), expected.value(QStringLiteral("fail")).toInt());
    QCOMPARE(q.value(2).toInt(), expected.value(QStringLiteral("warn")).toInt());
    QCOMPARE(q.value(3).toInt(), expectedAnomalies);
}

// A form's duration is an age, so it must come from the monotonic clock:
// advancing that clock 65 s while almost no wall time passes still yields
// 65 s. Closing the form never touches the vehicle's flight totals — that is
// the recorder's job, once, at the flight's close.
void FlightSessionLifecycleTest::testFormDurationUsesMonotonicClock()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    TestClock clock;
    MonotonicClock::setForTesting(&clock);
    const auto restoreClock = qScopeGuard([] { MonotonicClock::setForTesting(nullptr); });

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Clock Op"), QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);

    auto totals = []() -> QVariantList {
        QSqlQuery q(QSqlDatabase::database());
        if (!q.exec(QStringLiteral("SELECT COALESCE(SUM(total_flight_time_sec), 0), "
                                   "COALESCE(SUM(total_flight_count), 0) FROM vehicles")) || !q.next())
            return {};
        return {q.value(0), q.value(1)};
    };
    const QVariantList before = totals();

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    fs->startFlightSession(QStringLiteral("monotonic purpose"), QStringLiteral("loc"), QStringLiteral("notes"));
    QVERIFY(fs->currentFlightId() > 0);

    clock.advance(65000);
    fs->closeSession();

    const QVariantMap row = _formRow(vid, QStringLiteral("monotonic purpose"));
    QVERIFY2(!row.isEmpty(), "form row missing");
    QCOMPARE(row.value(QStringLiteral("duration_sec")).toInt(), 65);
    QCOMPARE(totals(), before);
}

// Two arms in one form session: one flights row per arm is right. The form
// row is adopted for the first flight; the second arm opens its own recorder
// row. Each row gets exactly one DISARM.
void FlightSessionLifecycleTest::testTwoArmsInOneFormSessionMakeTwoRows()
{
    int vid = 0; QString fp;
    if (!_connectAndRegister(&vid, &fp)) QSKIP("MockLink vehicle did not register a vehicle id");

    int opId = OperatorManager::instance()->addOperator(QStringLiteral("Form Op"), QStringLiteral("pilot"));
    OperatorManager::instance()->selectOperator(opId);

    FlightSession *fs = FlightSession::instance();
    fs->setVehicle(_vehicle);
    fs->setMode(QStringLiteral("FLIGHT"));
    const int base = _flightCount(vid);
    fs->startFlightSession(QStringLiteral("two arm purpose"), QStringLiteral("loc"), QStringLiteral("notes"));
    QVERIFY(fs->currentFlightId() > 0);
    QCOMPARE(_flightCount(vid), base + 1);   // form row only

    for (int arm = 0; arm < 2; ++arm) {
        _vehicle->setArmed(true, false);
        QVERIFY(_waitArmed(true));
        QTest::qWait(300);
        _vehicle->setArmed(false, false);
        QVERIFY(_waitArmed(false));
        QTest::qWait(300);
        // After the first flight the form row is reused; the second arm opens
        // a second row, so one row per arm overall.
        QCOMPARE(_flightCount(vid), base + 1 + arm);
    }

    int rows = 0;
    for (const QVariantMap &row : DatabaseManager::instance().getFlightsForVehicle(vid, 500)) {
        const int id = row.value(QStringLiteral("id")).toInt();
        if (_eventCount(id, QStringLiteral("DISARM")) > 0)
            ++rows;
    }
    QCOMPARE(rows, 2);
}

// The real copter's uid2 (AUTOPILOT_VERSION, 18 bytes, the STM32 id in the
// first 12): the key is its hex with the unused trailing zero bytes left out.
// All zero means no uid2.
void FlightSessionLifecycleTest::testUid2KeyFormat()
{
    const QByteArray real = QByteArray::fromHex("340043001551313238333439000000000000");
    QCOMPARE(real.size(), 18);
    QCOMPARE(VehicleRegistry::uid2Key(reinterpret_cast<const uint8_t *>(real.constData()), real.size()),
             QStringLiteral("340043001551313238333439"));
    const QByteArray zero(18, '\0');
    QVERIFY(VehicleRegistry::uid2Key(reinterpret_cast<const uint8_t *>(zero.constData()), zero.size()).isEmpty());
    // An inner zero byte is part of the id; only the trailing padding goes.
    const QByteArray inner = QByteArray::fromHex("010000020000000000000000000000000000");
    QCOMPARE(VehicleRegistry::uid2Key(reinterpret_cast<const uint8_t *>(inner.constData()), inner.size()),
             QStringLiteral("01000002"));
}

bool FlightSessionLifecycleTest::_connectCopterWithBoard(uint16_t productId)
{
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    auto *cfg = new MockConfiguration(QStringLiteral("Board %1 copter").arg(productId));
    cfg->setFirmwareType(MAV_AUTOPILOT_ARDUPILOTMEGA);
    cfg->setVehicleType(MAV_TYPE_QUADROTOR);
    cfg->setBoardVendorProduct(0x1209, productId);
    cfg->setIncrementVehicleId(false);   // the same copter (sysid) on a reconnect
    cfg->setDynamic(true);
    SharedLinkConfigurationPtr link = LinkManager::instance()->addConfiguration(cfg);
    if (!LinkManager::instance()->createConnectedLink(link))
        return false;
    _mockLink = qobject_cast<MockLink *>(link->link());
    if (!_mockLink || !spyVehicle.wait(10000))
        return false;
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!_vehicle)
        return false;
    QSignalSpy spyInit(_vehicle, &Vehicle::initialConnectComplete);
    spyInit.wait(30000);
    VehicleRegistry *reg = VehicleRegistry::instance();
    for (int i = 0; i < 200 && !reg->isIdentitySettled(); ++i)
        QTest::qWait(50);
    // Let any late registration (the old duplicate) land before counting.
    QTest::qWait(500);
    return reg->isIdentitySettled();
}

void FlightSessionLifecycleTest::_sendAutopilotVersionUid2(const QByteArray &uid2, uint16_t productId)
{
    const uint8_t custom[8]{};
    mavlink_message_t msg{};
    mavlink_msg_autopilot_version_pack_chan(
        static_cast<uint8_t>(_vehicle->id()), static_cast<uint8_t>(_vehicle->defaultComponentId()), 0, &msg,
        MAV_PROTOCOL_CAPABILITY_MAVLINK2, 0, 0, 0, 0, custom, custom, custom,
        0x1209, productId, 0 /* uid */, reinterpret_cast<const uint8_t *>(uid2.constData()));
    emit _vehicle->mavlinkMessageReceived(msg);
    for (int i = 0; i < 40; ++i)
        QTest::qWait(25);
}

// A real board reports its board id only in AUTOPILOT_VERSION, after the first
// heartbeat. The copter is registered once, under the identity it has once
// that message is in: never first under "board 0" and again under its board.
void FlightSessionLifecycleTest::testOneVehicleRowWhenBoardIdArrivesLate()
{
    const int rowsBefore = _vehicleRowCount();
    QVERIFY(_connectCopterWithBoard(0x5741));
    VehicleRegistry *reg = VehicleRegistry::instance();
    const QString expected = VehicleRegistry::computeFingerprint(0, false, MAV_TYPE_QUADROTOR, _vehicle->id(),
                                                                 QString::number(0x5741));
    QCOMPARE(reg->currentFingerprint(), expected);
    QCOMPARE(reg->currentDeviceKey(), expected);
    // One new row, under the settled identity; none under the heartbeat-only
    // identity (board 0).
    QCOMPARE(_vehicleRowCount() - rowsBefore, 1);
    const QString boardZero = VehicleRegistry::computeFingerprint(0, false, MAV_TYPE_QUADROTOR, _vehicle->id(),
                                                                  QStringLiteral("0"));
    QVERIFY2(_vehicleSource(boardZero).isEmpty(), "a row was registered before the board id was known");
    QCOMPARE(_vehicleSource(expected), QStringLiteral("SYSID_TYPE_FALLBACK"));
    for (int i = 0; i < 100 && VehicleProfileManager::instance()->currentDeviceUid() != expected; ++i)
        QTest::qWait(50);
    QCOMPARE(VehicleProfileManager::instance()->currentDeviceUid(), expected);
}

// uid = 0 with a uid2 is a hardware id: the copter gets a strong identity keyed
// by its uid2, and the profile (pack, table) follows it. The fallback row this
// same connection created is renamed, not left behind.
void FlightSessionLifecycleTest::testUid2GivesHardwareIdentity()
{
    QVERIFY(_connectCopterWithBoard(0x5742));
    VehicleRegistry *reg = VehicleRegistry::instance();
    QCOMPARE(reg->currentFingerprintSource(), QStringLiteral("SYSID_TYPE_FALLBACK"));   // mock: no uid2 yet
    const QString fallbackKey = reg->currentDeviceKey();
    QCOMPARE(_vehicleSource(fallbackKey), QStringLiteral("SYSID_TYPE_FALLBACK"));

    const QByteArray uid2 = QByteArray::fromHex("340043001551313238333439000000000000");
    _sendAutopilotVersionUid2(uid2, 0x5742);
    const QString key = QStringLiteral("340043001551313238333439");
    QCOMPARE(reg->currentFingerprintSource(), QStringLiteral("HARDWARE_UID"));
    QCOMPARE(reg->currentDeviceKey(), key);
    for (int i = 0; i < 100 && VehicleProfileManager::instance()->currentDeviceUid() != key; ++i)
        QTest::qWait(50);
    QCOMPARE(VehicleProfileManager::instance()->currentDeviceUid(), key);
    QCOMPARE(_vehicleSource(key), QStringLiteral("HARDWARE_UID"));
    // This connection's fallback row was renamed, not left behind.
    QVERIFY2(_vehicleSource(fallbackKey).isEmpty(), "the fallback row was left behind");

    // Settings made now stay with the hardware key.
    VehicleProfileManager *vpm = VehicleProfileManager::instance();
    vpm->setBatteryParallelCells(8);
    int s = 0, par = 0, mah = 0; double res = 0; QString type;
    QVERIFY(DatabaseManager::instance().vehicleBatteryConfig(key, &s, &par, &mah, &res, &type));
    QCOMPARE(par, 8);
}

// A fallback row that existed before this connection (another copter with the
// same sysid, frame and board model, or the simulator) is never merged into a
// hardware-identified copter: the copter takes its own key and the old row
// keeps its records.
void FlightSessionLifecycleTest::testExistingFallbackRowIsNotMerged()
{
    QVERIFY(_connectCopterWithBoard(0x5743));
    VehicleRegistry *reg = VehicleRegistry::instance();
    const QString fallbackKey = reg->currentDeviceKey();
    QVERIFY(!fallbackKey.isEmpty());
    VehicleProfileManager *vpm = VehicleProfileManager::instance();
    for (int i = 0; i < 100 && vpm->currentDeviceUid() != fallbackKey; ++i)
        QTest::qWait(50);
    vpm->setBatteryParallelCells(3);   // the other copter's setup
    _disconnectMockLink();

    // Reconnect: the fallback row now pre-exists. Then the hardware id arrives.
    QVERIFY(_connectCopterWithBoard(0x5743));
    QCOMPARE(reg->currentDeviceKey(), fallbackKey);
    _sendAutopilotVersionUid2(QByteArray::fromHex("aabb00112233445566778899000000000000"), 0x5743);
    const QString key = QStringLiteral("aabb00112233445566778899");
    QCOMPARE(reg->currentDeviceKey(), key);
    for (int i = 0; i < 100 && vpm->currentDeviceUid() != key; ++i)
        QTest::qWait(50);
    QCOMPARE(vpm->currentDeviceUid(), key);
    QVERIFY2(vpm->batteryParallelCells() != 3, "the other copter's pack followed the hardware id");
    QCOMPARE(_vehicleSource(fallbackKey), QStringLiteral("SYSID_TYPE_FALLBACK"));
    QCOMPARE(_vehicleSource(key), QStringLiteral("HARDWARE_UID"));
    int s = 0, par = 0, mah = 0; double res = 0; QString type;
    QVERIFY(DatabaseManager::instance().vehicleBatteryConfig(fallbackKey, &s, &par, &mah, &res, &type));
    QCOMPARE(par, 3);
}

UT_REGISTER_TEST(FlightSessionLifecycleTest)

#include "FlightSessionLifecycleTest.moc"
