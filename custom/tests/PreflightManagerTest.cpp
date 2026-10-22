#include <QTest>
#include <QSignalSpy>
#include <QSettings>

#include "UnitTest.h"
#include "PreflightManager.h"
#include "ParameterWatchlist.h"
#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "BatteryVoltageCheck.h"
#include "mocks/MockTelemetryBridge.h"
#include "utils/MonotonicClock.h"
#include "utils/TelemetryStream.h"

class PreflightManagerTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testInitialState();
    void testAddCheck();
    void testCheckById();
    void testProgressCounters();
    void testAllMandatoryPassed();
    void testEvaluateAll();
    void testArmingBlocker();
    void testResetAll();
    void testApplyVehicleKindBlocking();
    void testParamWatchlistSeededOnConnect();
    void testMarkArmedDisarmed();
    void testArmedSurvivesTick();
    void testResetForNextFlightClosesGate();
    void testConfirmationNotRestoredAtStartup();
    void testBatteryStreamStopMarksCheckStale();
    void testPassedCheckReevaluatedEachTick();
    void testNeverSeenStreamIsNotStale();
    void testUnhealthyBatteryMonitorIsNotCalledCritical();
};

void PreflightManagerTest::testInitialState()
{
    PreflightManager mgr;
    // Built-in phase-1 checks are auto-registered at construction
    QVERIFY(mgr.totalChecks() > 0);
    QCOMPARE(mgr.passedChecks(), 0);
    QCOMPARE(mgr.failedChecks(), 0);
    QCOMPARE(mgr.pendingChecks(), mgr.totalChecks());
    // No vehicle connected, so no kind is resolved and the mandatory set is
    // not configured — the gate must stay closed, never trivially pass (R16).
    QVERIFY(!mgr.allMandatoryPassed());
    QCOMPARE(mgr.completionPercent(), 0);
}

void PreflightManagerTest::testAddCheck()
{
    PreflightManager mgr;
    const int baseCount = mgr.totalChecks();

    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.manual.confirm"),
        QStringLiteral("Test Check"), CheckCategory::Safety,
        QStringLiteral("Test description"), QStringList(), QStringList(), &mgr);

    mgr.addCheck(check);
    QCOMPARE(mgr.totalChecks(), baseCount + 1);
    QVERIFY(mgr.checkById(QStringLiteral("test.manual.confirm")) != nullptr);
}

void PreflightManagerTest::testCheckById()
{
    PreflightManager mgr;
    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.findable"),
        QStringLiteral("Findable"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    mgr.addCheck(check);

    QVERIFY(mgr.checkById(QStringLiteral("test.findable")) != nullptr);
    QVERIFY(mgr.checkById(QStringLiteral("nonexistent")) == nullptr);
}

void PreflightManagerTest::testProgressCounters()
{
    PreflightManager mgr;
    const int baseCount = mgr.totalChecks();

    auto *check1 = new ManualConfirmCheck(
        QStringLiteral("test.a"), QStringLiteral("A"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    auto *check2 = new ManualConfirmCheck(
        QStringLiteral("test.b"), QStringLiteral("B"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    mgr.addCheck(check1);
    mgr.addCheck(check2);

    QCOMPARE(mgr.totalChecks(), baseCount + 2);
    QCOMPARE(mgr.pendingChecks(), baseCount + 2);

    check1->confirm();
    QCOMPARE(mgr.passedChecks(), 1);
    QCOMPARE(mgr.pendingChecks(), baseCount + 1);
}

void PreflightManagerTest::testAllMandatoryPassed()
{
    PreflightManager mgr;
    // Resolve a kind so the enforcement set is configured; without it the gate
    // is fail-closed by policy and the exercise below would be meaningless (R16).
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));

    // Built-in Auto checks need live telemetry and refuse confirm(); keep only
    // the controlled check mandatory so the assertion exercises the manager's
    // all-mandatory logic, not the telemetry mocks.
    for (auto *c : mgr.checks()) {
        c->setMandatory(false);
    }

    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.mand"), QStringLiteral("Mand"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    check->setMandatory(true);
    mgr.addCheck(check);

    QVERIFY(!mgr.allMandatoryPassed());

    check->confirm();
    QVERIFY(mgr.allMandatoryPassed());
}

void PreflightManagerTest::testEvaluateAll()
{
    PreflightManager mgr;
    QSignalSpy progressSpy(&mgr, &PreflightManager::progressChanged);

    // Add a test check that has a real evaluate() override
    class AutoCheck : public AbstractCheck {
    public:
        using AbstractCheck::AbstractCheck;
        void evaluate() override {
            setStatus(CheckStatus::Passed, QStringLiteral("Auto-pass"));
        }
    };

    auto *check = new AutoCheck(
        QStringLiteral("test.auto"), QStringLiteral("Auto"), CheckCategory::Safety,
        CheckType::Auto, true, false, &mgr);
    mgr.addCheck(check);

    mgr.evaluateAll();
    QCOMPARE(check->status(), CheckStatus::Passed);
    QVERIFY(progressSpy.count() > 0);
}

void PreflightManagerTest::testArmingBlocker()
{
    PreflightManager mgr;
    QVERIFY(mgr.armingBlocker().isEmpty());

    class FailCheck : public AbstractCheck {
    public:
        using AbstractCheck::AbstractCheck;
        void evaluate() override {
            setStatus(CheckStatus::Failed, QStringLiteral("Battery low"));
        }
    };

    auto *check = new FailCheck(
        QStringLiteral("test.fail"), QStringLiteral("Fail"), CheckCategory::Power,
        CheckType::Auto, true, false, &mgr);
    mgr.addCheck(check);

    mgr.evaluateAll();
    QVERIFY(!mgr.armingBlocker().isEmpty());
    QVERIFY(mgr.armingBlocker().contains(QStringLiteral("Battery low")));
}

void PreflightManagerTest::testResetAll()
{
    PreflightManager mgr;
    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.rst"), QStringLiteral("Rst"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    mgr.addCheck(check);

    check->confirm();
    QCOMPARE(check->status(), CheckStatus::Passed);

    QSignalSpy progressSpy(&mgr, &PreflightManager::progressChanged);
    mgr.resetAll();

    QCOMPARE(check->status(), CheckStatus::Pending);
    QVERIFY(progressSpy.count() > 0);
}

void PreflightManagerTest::testParamWatchlistSeededOnConnect()
{
    QVERIFY(ParameterWatchlist::names().size() > 0);

    MockTelemetryBridge bridge;
    PreflightManager mgr;
    mgr.setTelemetryBridge(&bridge);

    // Before any vehicle connects the watchlist is empty: notifyParamReceived
    // has nothing to fill and the ParamLoading FSM leg stays dead (R3).
    QCOMPARE(mgr.paramManager()->totalCount(), 0);

    // Simulate vehicle connect. setWatchlist must seed the full parameter
    // roster so parameter arrival can drive the watchlist to Ready.
    bridge.setConnected(true);
    QVERIFY(mgr.paramManager()->totalCount() == ParameterWatchlist::names().size());

    // Deliver every watched parameter; manager should reach Ready, not hang
    // in Loading forever (R3 regression guard).
    for (const QString &name : ParameterWatchlist::names()) {
        bridge.emitParameterUpdated(name, 1.0f);
    }
    QVERIFY(mgr.paramManager()->isReady());
}

void PreflightManagerTest::testApplyVehicleKindBlocking()
{
    PreflightManager mgr;

    auto *motorCount = mgr.checkById(QStringLiteral("airframe.motor_count"));
    auto *motorSpin  = mgr.checkById(QStringLiteral("propulsion.motors.spin"));
    auto *rtlAlt     = mgr.checkById(QStringLiteral("safety.rtl_alt"));

    QVERIFY(motorCount && motorSpin && rtlAlt);

    // Defaults before any kind is applied
    QVERIFY(!motorCount->isBlocking());
    QVERIFY(!rtlAlt->isBlocking());

    // Multirotor: motors block, rtl_alt advisory.
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    QVERIFY(motorCount->isBlocking());
    QVERIFY(motorSpin->isBlocking());
    QVERIFY(!rtlAlt->isBlocking());

    // Fixed wing: rtl_alt blocks; motor checks advisory.
    mgr.applyVehicleKind(QStringLiteral("FIXED_WING"));
    QVERIFY(!motorCount->isBlocking());
    QVERIFY(!motorSpin->isBlocking());
    QVERIFY(rtlAlt->isBlocking());

    // Unknown kind leaves the previous explicit decisions untouched.
    mgr.applyVehicleKind(QStringLiteral("UNKNOWN"));
    QVERIFY(rtlAlt->isBlocking());
}

void PreflightManagerTest::testMarkArmedDisarmed()
{
    PreflightManager mgr;
    QCOMPARE(mgr.state(), PreflightStateMachine::Disconnected);

    // Resolve a kind and pass every check so the post-disarm FSM is driven by
    // real (green) check results rather than an unconfigured gate (R16).
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    for (auto *c : mgr.checks()) {
        c->setMandatory(false);
    }

    // Confirmed arm must drive the FSM to the Armed terminal (R8). Before the
    // fix the Armed state was defined but no caller ever entered it.
    mgr.markArmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    // Disarm retreats: all mandatory checks pass, so the FSM lands back on
    // ArmingAllowed.
    mgr.markDisarmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::ArmingAllowed);
}

void PreflightManagerTest::testArmedSurvivesTick()
{
    PreflightManager mgr;
    // Resolve + confirm so the post-disarm expectation is driven by check
    // results (R16); the Armed-retention assertions are the R8 regressions.
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    for (auto *c : mgr.checks()) {
        c->setMandatory(false);
    }

    mgr.markArmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    // A periodic tick must NOT retreat an armed vehicle back to
    // ChecklistInProgress even if check results degrade — leaving Armed is
    // exclusively owned by markDisarmed() (R8).
    mgr.tick();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    mgr.markDisarmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::ArmingAllowed);
}

// Step 1.5 (post-flight bypass): after a disarm with post-flight parked, every
// check is Pending again and the gate is closed until preflight is re-run.
// The old path marked every check Skipped, which allMandatoryPassed() counts
// as passed, so a re-arm needed no checks at all.
void PreflightManagerTest::testResetForNextFlightClosesGate()
{
    PreflightManager mgr;
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    for (auto *c : mgr.checks())
        c->setMandatory(false);

    auto *props = new ManualConfirmCheck(
        QStringLiteral("test.props_removed"), QStringLiteral("Props"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    props->setMandatory(true);
    mgr.addCheck(props);

    QVERIFY(props->confirm());
    QVERIFY(mgr.allMandatoryPassed());
    mgr.markArmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    mgr.resetForNextFlight();

    QCOMPARE(props->status(), CheckStatus::Pending);
    QVERIFY2(!mgr.allMandatoryPassed(), "gate still open after disarm — post-flight bypass");
    QVERIFY(mgr.state() != PreflightStateMachine::Armed);
    QVERIFY(mgr.state() != PreflightStateMachine::ArmingAllowed);
}

// Step 1.6 (F16): a confirmation stored by an earlier run must not be
// re-applied when the next PreflightManager starts.
void PreflightManagerTest::testConfirmationNotRestoredAtStartup()
{
    const QString id = QStringLiteral("airframe.visual_inspection");
    {
        PreflightManager first;
        AbstractCheck *c = first.checkById(id);
        QVERIFY(c);
        QVERIFY(c->confirm(QStringLiteral("inspected yesterday")));
        first.persistOverrides();
    }
    {
        PreflightManager next;
        next.setDatabaseManager(nullptr);   // the old startup path re-applied overrides here
        AbstractCheck *c = next.checkById(id);
        QVERIFY(c);
        QVERIFY2(c->status() == CheckStatus::Pending,
                 "a confirmation from an earlier run was restored at startup");
    }
    QSettings settings;
    settings.remove(QStringLiteral("preflight_overrides"));
}

namespace {
// Installs a TestClock for one test and always restores the steady clock.
struct ScopedTestClock {
    TestClock clock;
    ScopedTestClock() { MonotonicClock::setForTesting(&clock); }
    ~ScopedTestClock() { MonotonicClock::setForTesting(nullptr); }
};
}

// Step 2.2 (F1, F3): BATTERY_STATUS stops while everything else is fine → the
// battery check goes Stale within 5 s instead of passing on the cached voltage,
// and recovers on the first tick after the stream resumes.
void PreflightManagerTest::testBatteryStreamStopMarksCheckStale()
{
    ScopedTestClock t;
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);   // hasTelemetry() needs quality >= 10
    bridge.setBatteryVoltage(16.8);
    bridge.setBatteryPercent(80);
    PreflightManager mgr;
    mgr.setTelemetryBridge(&bridge);
    AbstractCheck *batt = mgr.checkById(QStringLiteral("power.battery.voltage"));
    QVERIFY(batt);

    bridge.stampStream(TelemetryStream::BatteryStatus);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Passed);

    t.clock.advance(4000);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Passed);   // 4 s: within the 5 s limit

    t.clock.advance(2000);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Stale);    // 6 s without BATTERY_STATUS

    bridge.stampStream(TelemetryStream::BatteryStatus);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Passed);   // stream resumed
}

// Step 2.2 (F2): a Passed check is re-evaluated on every tick, so a value that
// degrades after passing fails on the next tick.
void PreflightManagerTest::testPassedCheckReevaluatedEachTick()
{
    ScopedTestClock t;
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);   // hasTelemetry() needs quality >= 10
    bridge.setBatteryVoltage(16.8);
    bridge.setBatteryPercent(80);
    PreflightManager mgr;
    mgr.setTelemetryBridge(&bridge);
    AbstractCheck *batt = mgr.checkById(QStringLiteral("power.battery.voltage"));
    QVERIFY(batt);

    bridge.stampStream(TelemetryStream::BatteryStatus);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Passed);

    QSignalSpy spy(batt, &AbstractCheck::statusChanged);
    bridge.blockSignals(true);                 // no change notification: only the tick may notice
    bridge.setBatteryVoltage(9.0);
    bridge.setBatteryPercent(0);
    bridge.blockSignals(false);
    bridge.stampStream(TelemetryStream::BatteryStatus);
    t.clock.advance(500);
    mgr.tick();
    QCOMPARE(batt->status(), CheckStatus::Failed);
}

// A stream the autopilot never sent is not "stale": the check's own evaluate()
// reports missing data. Otherwise an unsent stream would block arming forever.
void PreflightManagerTest::testNeverSeenStreamIsNotStale()
{
    ScopedTestClock t;
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);   // hasTelemetry() needs quality >= 10
    bridge.setBatteryVoltage(16.8);
    bridge.setBatteryPercent(80);
    PreflightManager mgr;
    mgr.setTelemetryBridge(&bridge);
    AbstractCheck *batt = mgr.checkById(QStringLiteral("power.battery.voltage"));
    QVERIFY(batt);

    t.clock.advance(60000);
    mgr.tick();
    QVERIFY(batt->status() != CheckStatus::Stale);
}

// ArduPilot's MAV_BATTERY_CHARGE_STATE_UNHEALTHY is a battery-MONITOR fault
// (sensor / configuration), not a low charge. The check still blocks (like
// ArduPilot's own pre-arm) but must say so instead of "Battery state: critical".
void PreflightManagerTest::testUnhealthyBatteryMonitorIsNotCalledCritical()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    bridge.setBatteryVoltage(16.6);
    bridge.setBatteryPercent(90);
    BatteryVoltageCheck check(&bridge, 0.0, 5.0);
    // setTestValue() is private but Q_INVOKABLE (this test is not a friend of AbstractCheck).
    QVERIFY(QMetaObject::invokeMethod(&check, "setTestValue",
                                      Q_ARG(QString, QStringLiteral("batteryChargeState")),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("unhealthy")))));

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
    QVERIFY2(check.message().contains(QStringLiteral("MONITOR")), qPrintable(check.message()));
    QVERIFY2(!check.message().contains(QStringLiteral("critical")), qPrintable(check.message()));
}

UT_REGISTER_TEST(PreflightManagerTest)

#include "PreflightManagerTest.moc"
