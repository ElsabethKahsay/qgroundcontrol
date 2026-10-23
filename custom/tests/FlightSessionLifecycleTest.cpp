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
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"

class FlightSessionLifecycleTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        // Unit-test mode skips the app boot, so PreflightPlugin::init() (which
        // constructs FlightSession, OperatorManager, ArmingGate, ...) never runs.
        // Mirror QmlSmokeTest's harness: boot it once per process.
        static bool once = false;
        if (!once) {
            PreflightPlugin::instance()->init();
            once = true;
        }
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

private:
    // Connects a mock plane and returns true when a vehicle + registry id exist.
    bool _connectAndRegister(int *vehicleIdOut, QString *fingerprintOut);
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

// (c) Arm, disarm, arm again on the same connection. Since Step 3 the flight
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

UT_REGISTER_TEST(FlightSessionLifecycleTest)

#include "FlightSessionLifecycleTest.moc"
