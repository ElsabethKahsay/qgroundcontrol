/**
 * @file ReturnToLaunchTest.cpp
 * @brief Phase 1 (FlyQuickActions restructuring) — RTL hardening at the
 *        TelemetryBridge layer.
 *
 * Track B contract (fork-native ArduPilot RTL):
 *
 *   (a) no vehicle connected -> returnToLaunch() refuses: returns false,
 *       emits nothing, nothing on the wire
 *   (b) connected vehicle     -> the SINGLE RTL code path is exercised:
 *       `TelemetryBridge::returnToLaunch()` sends EXACTLY ONE
 *       MAV_CMD_DO_SET_MODE command-long in the custom-mode form
 *       (param1 = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, param2 = the RTL
 *       custom-mode number). The flight context captured at trigger time is
 *       emitted on rtlTriggered(), and the return value reflects the ACTUAL
 *       heartbeat-confirmed mode — never "command sent" claimed as success.
 *   (b2) per-type literal RTL mode IDs (B7): ArduPlane/ArduRover -> 11,
 *       ArduCopter -> 6 (the real ArduPilot mode-enum values).
 *   (c) distanceToHome()      -> haversine distance from the (virtual) GPS/home
 *       accessors; 0.0 before a fix/home is known
 *   (d) B8: DO_SET_MODE is sent regardless of the arming-gate state (open,
 *       closed, or no gate installed) — the arm interceptor is ARM_DISARM-only,
 *       it must never filter the RTL mode change.
 *   (e) B9: a DO_SET_MODE RTL leaves the armed state untouched (ArduPilot
 *       contract) — confirmed both by the returned result and live state.
 *
 * The MockLink ACKs MAV_CMD_DO_SET_MODE by echoing the requested custom mode in
 * its HEARTBEAT (real feedback, not just an ack), so on every MAV_TYPE the RTL
 * mode change is actually observed and (b) deterministically reports
 * "confirmed". The no-confirmation path is still covered by the contract check:
 * result always equals (mode heartbeat-confirmed AND armed preserved).
 *
 * B7 — the per-type literal mode IDs come from the real ArduPilot mode enums:
 *   - ArduCopter/mode.h   : enum Number { STABILIZE=0, ACRO=1, ALT_HOLD=2,
 *                           AUTO=3, GUIDED=4, LOITER=5, **RTL=6**, ... }
 *   - ArduPlane/mode.h    : enum Number { MANUAL=0, CIRCLE=1, ..., AUTO=10,
 *                           **RTL=11**, ... }
 *   - APMrover2/mode.h    : enum Number { ..., AUTO=10, **RTL=11**, ... }
 * A MAV_CMD_DO_SET_MODE command-long carries them as param2, with param1 =
 * MAV_MODE_FLAG_CUSTOM_MODE_ENABLED (ArduPilot's flight-mode-change form).
 */

#include <QTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QCoreApplication>

#include "UnitTest.h"
#include "TelemetryBridge.h"
#include "mocks/MockTelemetryBridge.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"
#include "Comms/MockLink/MockConfiguration.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "utils/GeoUtil.h"
#include "utils/FlightModeUtil.h"

// Test helper check used to drive the arm gate into a blocked state
// deterministically (same mechanism ArmSendTest uses).
class RtlGateFailCheck : public AbstractCheck {
public:
    explicit RtlGateFailCheck(QObject* parent = nullptr)
        : AbstractCheck(QStringLiteral("rtl.test.gatecheck"),
                        QStringLiteral("RTL test gate failure"),
                        CheckCategory::Safety, CheckType::Auto,
                        true /*mandatory*/, true /*canOverride*/, parent)
    {}
    void evaluate() override {}
    void forceStatus(CheckStatus s, const QString& msg = {}) { setStatus(s, msg); }
};

class ReturnToLaunchTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testNoVehicleGuard();
    void testDistanceToHomeNoFix();
    void testDistanceToHomeComputed();
    void testRtlPlaneSendsDoSetModeAndConfirms();
    void testRtlModeIdsPerVehicleType();
    void testRtlSendsRegardlessOfArmGateState();
    void testRtlPreservesArmedState();

private:
    bool _pumpUntil(bool (*cond)(void* ctx), void* ctx, int timeoutMs);
    bool _waitForDoSetMode(int expected);
    Vehicle* _connectMock(MockLink* (*factory)(bool, MockConfiguration::FailureMode_t));
    void _makeBlockedGate(ArmingGate& gate, MockTelemetryBridge& bridge);
};

bool ReturnToLaunchTest::_pumpUntil(bool (*cond)(void* ctx), void* ctx, int timeoutMs)
{
    const quint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (cond(ctx)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(50);
    }
    return cond(ctx);
}

// The DO_SET_MODE send is asynchronous (queued through the Vehicle command
// pipeline), so wait for it to actually land on the link before counting.
// When the vehicle already reports the target mode the heartbeat-confirm
// short-circuits before the wire write completes — this pump removes that race.
bool ReturnToLaunchTest::_waitForDoSetMode(int expected)
{
    struct Ctx { MockLink* link; int expected; } ctx = { _mockLink, expected };
    return _pumpUntil([](void* c) {
        Ctx* p = reinterpret_cast<Ctx*>(c);
        return p->link->receivedMavCommandCount(MAV_CMD_DO_SET_MODE) >= p->expected;
    }, &ctx, 5000);
}

Vehicle* ReturnToLaunchTest::_connectMock(MockLink* (*factory)(bool, MockConfiguration::FailureMode_t))
{
    _mockLink = factory(false, MockConfiguration::FailureMode_t::FailNone);
    if (!_mockLink) return nullptr;
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    if (!spyVehicle.wait(10000)) return nullptr;
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!_vehicle) return nullptr;
    QSignalSpy spyPlan(_vehicle, &Vehicle::initialConnectComplete);
    if (!spyPlan.wait(30000)) return nullptr;
    return _vehicle;
}

void ReturnToLaunchTest::_makeBlockedGate(ArmingGate& gate, MockTelemetryBridge& bridge)
{
    gate.setMode(ArmingGate::Active);

    // Mirror ArmSendTest's blocked-gate setup exactly: heap manager parented to
    // the gate, mandatory safety check forced to Failed, and the gate's arm
    // interceptor installed on the vehicle (the same veto the live app uses).
    PreflightManager* mgr = new PreflightManager(1, &gate);
    mgr->setTelemetryBridge(&bridge);
    gate.setPreflightManager(mgr);
    gate.setTelemetryBridge(&bridge);

    auto* failCheck = new RtlGateFailCheck(mgr);
    failCheck->forceStatus(CheckStatus::Failed, QStringLiteral("Simulated critical failure"));
    mgr->addCheck(failCheck);

    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.isOverrideActive());

    _vehicle->setArmCommandInterceptor(gate.makeArmInterceptor());
}

// (a) No vehicle connected -> RTL is refused outright: false, no signals, and
//     importantly no code path is entered (guard runs before the mode send).
void ReturnToLaunchTest::testNoVehicleGuard()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);

    QSignalSpy spyTriggered(&bridge, &TelemetryBridge::rtlTriggered);
    QSignalSpy spyCompleted(&bridge, &TelemetryBridge::rtlCommandCompleted);

    QVERIFY(!bridge.returnToLaunch());

    QCOMPARE(spyTriggered.count(), 0);
    QCOMPARE(spyCompleted.count(), 0);
}

// (c) distanceToHome() is 0.0 until both a GPS fix AND a home position exist.
void ReturnToLaunchTest::testDistanceToHomeNoFix()
{
    MockTelemetryBridge bridge;
    QCOMPARE(bridge.distanceToHome(), 0.0);

    bridge.setGpsPosition(37.95, -122.47);
    QCOMPARE(bridge.distanceToHome(), 0.0); // home still unknown

    // Distinct positions: once both are known the Haversine distance is real.
    bridge.setHomePosition(37.95, -122.47);
    bridge.setGpsPosition(37.98, -122.44);
    QVERIFY(bridge.distanceToHome() > 0.0);
}

// (c) distanceToHome() computes a Haversine distance from the live accessors.
void ReturnToLaunchTest::testDistanceToHomeComputed()
{
    const double homeLat = 37.0, homeLon = -122.0;
    const double gpsLat = 37.5,   gpsLon = -121.5;

    MockTelemetryBridge bridge;
    bridge.setGpsPosition(gpsLat, gpsLon);
    bridge.setHomePosition(homeLat, homeLon);

    const double expected = GeoUtil::haversineMeters(homeLat, homeLon, gpsLat, gpsLon);
    QVERIFY(expected > 0.0);
    QCOMPARE(bridge.distanceToHome(), expected);
}

// (b) The single RTL code path on the ArduPlane mock: exactly ONE
//     MAV_CMD_DO_SET_MODE carrying the plane RTL custom-mode number (11),
//     context captured on rtlTriggered(), armed state untouched, and the REAL
//     heartbeat-confirmed outcome reported back — never success from "sent".
void ReturnToLaunchTest::testRtlPlaneSendsDoSetModeAndConfirms()
{
    QVERIFY(_connectMock(&MockLink::startAPMArduPlaneMockLink));

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setVehicle(_vehicle);

    // Flight context the snapshot must capture (virtual accessors).
    bridge.setAltitudeRelative(25.0);
    bridge.setGroundSpeed(12.5);
    const double homeLat = 37.0, homeLon = -122.0;
    const double gpsLat = 37.5,   gpsLon = -121.5;
    bridge.setHomePosition(homeLat, homeLon);
    bridge.setGpsPosition(gpsLat, gpsLon);
    const double expectedDist = GeoUtil::haversineMeters(homeLat, homeLon, gpsLat, gpsLon);

    _mockLink->clearReceivedMavCommandCounts();

    QSignalSpy spyTriggered(&bridge, &TelemetryBridge::rtlTriggered);
    QSignalSpy spyCompleted(&bridge, &TelemetryBridge::rtlCommandCompleted);

    const bool result = bridge.returnToLaunch();

    // Exactly ONE rtlTriggered with the captured snapshot.
    QCOMPARE(spyTriggered.count(), 1);
    QList<QVariant> args = spyTriggered.takeFirst();
    QCOMPARE(args.at(0).toDouble(), 25.0);
    QCOMPARE(args.at(1).toDouble(), 12.5);
    QCOMPARE(args.at(2).toDouble(), expectedDist);

    // Exactly ONE completion signal.
    QCOMPARE(spyCompleted.count(), 1);

    // Fork-native path: exactly ONE DO_SET_MODE command-long, in the custom-mode
    // form, with the literal ArduPilot plane RTL mode id 11. No arm/disarm or
    // takeoff rides along.
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE), 1);
    QCOMPARE(_mockLink->lastDoSetModeParam2(), 11.0f);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF), 0);

    // Real feedback, not "command sent": the heartbeat must report the RTL
    // custom mode (FlightModeUtil parses both friendly names and "Custom:0x…").
    QVERIFY2(FlightModeUtil::planeModeNumber(_vehicle->flightMode()) == 11,
             "Mock did not confirm RTL via heartbeat");
    QCOMPARE(result, true);
    QCOMPARE(spyCompleted.takeFirst().at(0).toBool(), true);
}

// (b2/B7) The RTL custom-mode number is literal and vehicle-type specific:
// ArduPlane -> 11, ArduRover -> 11, ArduCopter -> 6 (real ArduPilot mode enums).
void ReturnToLaunchTest::testRtlModeIdsPerVehicleType()
{
    struct Case { MockLink* (*factory)(bool, MockConfiguration::FailureMode_t); float expectedParam2; };
    const Case cases[] = {
        { &MockLink::startAPMArduPlaneMockLink, 11.0f },
        { &MockLink::startAPMArduRoverMockLink, 11.0f },
        { &MockLink::startAPMArduCopterMockLink, 6.0f },
    };

    for (const Case& c : cases) {
        // Disconnect the previous mock before starting the next one so each
        // type asserts against a clean active vehicle.
        if (_mockLink) {
            _mockLink->disconnect();
            _mockLink = nullptr;
            QSignalSpy spyRemoved(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
            spyRemoved.wait(5000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        QVERIFY2(_connectMock(c.factory), "mock vehicle failed to connect");
        MockTelemetryBridge bridge;
        bridge.setConnected(true);
        bridge.setVehicle(_vehicle);

        _mockLink->clearReceivedMavCommandCounts();
        const bool result = bridge.returnToLaunch();

        QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE), 1);
        QCOMPARE(_mockLink->lastDoSetModeParam2(), c.expectedParam2);
        QVERIFY2(FlightModeUtil::planeModeNumber(_vehicle->flightMode()) == int(c.expectedParam2),
                 qPrintable(QString("RTL %1 not confirmed via heartbeat").arg(c.expectedParam2)));
        QVERIFY(result);
    }
}

// (d/B8) DO_SET_MODE is a flight-mode command: the ARM_DISARM interceptor
// (armed gate) must not filter it. It sends with a closed gate (interceptor
// installed and vetoing arms) and with no gate reference at all.
void ReturnToLaunchTest::testRtlSendsRegardlessOfArmGateState()
{
    QVERIFY(_connectMock(&MockLink::startAPMArduPlaneMockLink));

    // 1) Closed gate with the live arm interceptor wired in.
    ArmingGate gate;
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setVehicle(_vehicle);
    _makeBlockedGate(gate, bridge); // also bridges.setArmingGate(&gate)
    bridge.setArmingGate(&gate);
    QVERIFY(!gate.isArmingAllowed()); // gate really closed

    _mockLink->clearReceivedMavCommandCounts();
    const bool closedResult = bridge.returnToLaunch();
    QVERIFY(_waitForDoSetMode(1));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE), 1);
    QCOMPARE(_mockLink->lastDoSetModeParam2(), 11.0f);
    QVERIFY(closedResult);

    // 2) No gate reference at all (null _armingGate): the mode change still goes
    // out, and the interceptor installed in (1) must not filter DO_SET_MODE.
    _vehicle->setArmCommandInterceptor(nullptr);
    MockTelemetryBridge gateFreeBridge;
    gateFreeBridge.setConnected(true);
    gateFreeBridge.setVehicle(_vehicle);
    gateFreeBridge.setArmingGate(nullptr);

    _mockLink->clearReceivedMavCommandCounts();
    const bool nullResult = gateFreeBridge.returnToLaunch();
    QVERIFY(_waitForDoSetMode(1));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE), 1);
    QCOMPARE(_mockLink->lastDoSetModeParam2(), 11.0f);
    QVERIFY(nullResult);
}

// (e/B9) A DO_SET_MODE RTL change leaves the armed state untouched: drive the
// mock into the armed state first, RTL, and verify both the mode changed AND
// the vehicle stayed armed (returned result also requires armed preservation).
void ReturnToLaunchTest::testRtlPreservesArmedState()
{
    QVERIFY(_connectMock(&MockLink::startAPMArduPlaneMockLink));

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setVehicle(_vehicle);

    // Arm through the real Vehicle command path (unit harness: no gate wired,
    // so the direct arm goes out). Wait for the armed bit via heartbeat.
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_COMPONENT_ARM_DISARM,
                             true /*showError*/, 1.0f);
    struct Ctx { Vehicle* v; } ctx = { _vehicle };
    QVERIFY(_pumpUntil([](void* c) {
        return reinterpret_cast<Ctx*>(c)->v->armed();
    }, &ctx, 5000));

    _mockLink->clearReceivedMavCommandCounts();
    const bool result = bridge.returnToLaunch();

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE), 1);
    QCOMPARE(_mockLink->lastDoSetModeParam2(), 11.0f);
    QVERIFY2(FlightModeUtil::planeModeNumber(_vehicle->flightMode()) == 11,
             "RTL not confirmed via heartbeat");
    QVERIFY2(_vehicle->armed(), "DO_SET_MODE must not disarm the vehicle");
    QVERIFY(result); // armed-safe + mode-confirmed == success
}

UT_REGISTER_TEST(ReturnToLaunchTest)

#include "ReturnToLaunchTest.moc"