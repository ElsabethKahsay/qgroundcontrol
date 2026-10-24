#include <QTest>
#include <QSignalSpy>
#include <QSettings>

#include "UnitTest.h"
#include "PreflightManager.h"
#include "ParameterWatchlist.h"
#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "BatteryVoltageCheck.h"
#include "GcsFailsafeCheck.h"
#include "RadioFailsafeCheck.h"
#include "AttitudeCheck.h"
#include "AirspeedSensorCheck.h"
#include <QtMath>
#include "mocks/MockTelemetryBridge.h"
#include "utils/DatabaseManager.h"
#include "utils/MonotonicClock.h"
#include "utils/TelemetryStream.h"

class PreflightManagerTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
        DatabaseManager::instance().initialize();
    }
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
    void testGcsFailsafeUsesPlaneParameterName();
    void testGcsFailsafeUsesCopterParameterName();
    void testGcsFailsafePolicyMatrix();
    void testGcsFailsafeNoPolicyWarnsAndPolicyIsStoredPerVehicle();
    void testGcsPolicyIsPerAirframeNotPerSystemId();
    void testSafetyCheckParametersAreWatched();
    void testRadioFailsafeReadsPlaneParameters();
    void testRadioFailsafeReadsCopterParameters();
    void testAttitudeFailurePointsToOrientation();
    void testAirspeedSensorConfiguredButMissing();
    void testInformationalChecksNeverGateArming();
    void testMandatorySetPerAirframe();
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
    // not configured — the gate must stay closed, never trivially pass.
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
    // is fail-closed by policy and the exercise below would be meaningless.
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
    // has nothing to fill and the ParamLoading FSM leg stays dead.
    QCOMPARE(mgr.paramManager()->totalCount(), 0);

    // Simulate vehicle connect. setWatchlist must seed the full parameter
    // roster so parameter arrival can drive the watchlist to Ready.
    bridge.setConnected(true);
    QVERIFY(mgr.paramManager()->totalCount() == ParameterWatchlist::names().size());

    // Deliver every watched parameter; manager should reach Ready, not hang
    // in Loading forever.
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
    // real (green) check results rather than an unconfigured gate.
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    for (auto *c : mgr.checks()) {
        c->setMandatory(false);
    }

    // Confirmed arm must drive the FSM to the Armed terminal. Before the
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
    // results; the assertions below check that Armed is kept until disarm.
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    for (auto *c : mgr.checks()) {
        c->setMandatory(false);
    }

    mgr.markArmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    // A periodic tick must NOT retreat an armed vehicle back to
    // ChecklistInProgress even if check results degrade — leaving Armed is
    // exclusively owned by markDisarmed().
    mgr.tick();
    QCOMPARE(mgr.state(), PreflightStateMachine::Armed);

    mgr.markDisarmed();
    QCOMPARE(mgr.state(), PreflightStateMachine::ArmingAllowed);
}

// Post-flight bypass: after a disarm with post-flight parked, every
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

// A confirmation stored by an earlier run must not be
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

// BATTERY_STATUS stops while everything else is fine → the
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

// A Passed check is re-evaluated on every tick, so a value that
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

namespace {

bool setTestOverride(AbstractCheck &check, const QString &name, const QVariant &value)
{
    return QMetaObject::invokeMethod(&check, "setTestValue",
                                     Q_ARG(QString, name), Q_ARG(QVariant, value));
}

void clearGcsPolicy(const QString &checkId)
{
    // check_config.value is NOT NULL; "unset" is not a valid policy, so
    // GcsFailsafeCheck::policy() reports it as no stored policy.
    DatabaseManager::instance().setCheckConfig(checkId, QStringLiteral("policy"),
                                               QStringLiteral("unset"), 0);
}

} // namespace

// ArduPlane 4.5 calls the parameter FS_GCS_ENABL, not ArduCopter's FS_GCS_ENABLE.
// The check must read the plane's name and honour the telemetry-primary policy.
void PreflightManagerTest::testGcsFailsafeUsesPlaneParameterName()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    GcsFailsafeCheck check(&bridge);
    clearGcsPolicy(check.id());

    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"),
                            QVariant(QStringLiteral("FixedWing"))));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_GCS_ENABL"), QVariant(1.0)));
    QVERIFY(DatabaseManager::instance().setCheckConfig(check.id(), QStringLiteral("policy"),
                                                       QStringLiteral("telemetry_primary"), 0));

    QCOMPARE(check.parameterName(), QStringLiteral("FS_GCS_ENABL"));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QVERIFY2(check.message().contains(QStringLiteral("FS_GCS_ENABL")),
             qPrintable(check.message()));
    QVERIFY2(!check.message().contains(QStringLiteral("FS_GCS_ENABLE")),
             qPrintable(check.message()));

    clearGcsPolicy(check.id());
}

// ArduCopter 4.5 uses FS_GCS_ENABLE. The same check must read that name on a copter.
void PreflightManagerTest::testGcsFailsafeUsesCopterParameterName()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    GcsFailsafeCheck check(&bridge);
    clearGcsPolicy(check.id());

    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"),
                            QVariant(QStringLiteral("Quadrotor"))));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_GCS_ENABLE"), QVariant(1.0)));
    QVERIFY(DatabaseManager::instance().setCheckConfig(check.id(), QStringLiteral("policy"),
                                                       QStringLiteral("telemetry_primary"), 0));

    QCOMPARE(check.parameterName(), QStringLiteral("FS_GCS_ENABLE"));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QVERIFY2(check.message().contains(QStringLiteral("FS_GCS_ENABLE")),
             qPrintable(check.message()));

    clearGcsPolicy(check.id());
}

// Passed only when the vehicle setting matches the chosen policy.
void PreflightManagerTest::testGcsFailsafePolicyMatrix()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    GcsFailsafeCheck check(&bridge);
    clearGcsPolicy(check.id());

    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"),
                            QVariant(QStringLiteral("FixedWing"))));

    struct Case {
        const char *policy;
        double setting;
        CheckStatus expected;
    };
    const Case cases[] = {
        { "rc_primary",         0.0, CheckStatus::Passed },
        { "rc_primary",         1.0, CheckStatus::Failed },
        { "telemetry_primary",  0.0, CheckStatus::Failed },
        { "telemetry_primary",  1.0, CheckStatus::Passed },
    };

    for (const Case &c : cases) {
        QVERIFY(setTestOverride(check, QStringLiteral("param_FS_GCS_ENABL"),
                                QVariant(c.setting)));
        QVERIFY(DatabaseManager::instance().setCheckConfig(
            check.id(), QStringLiteral("policy"), QString::fromLatin1(c.policy), 0));

        check.evaluate();
        QVERIFY2(check.status() == c.expected,
                 qPrintable(QStringLiteral("policy=%1 setting=%2 status=%3 message=%4")
                                .arg(QString::fromLatin1(c.policy))
                                .arg(c.setting)
                                .arg(check.statusInt())
                                .arg(check.message())));
    }

    clearGcsPolicy(check.id());
}

// No stored policy is a Warning; choosing one writes it for the current vehicle
// and invalid choices are refused.
void PreflightManagerTest::testGcsFailsafeNoPolicyWarnsAndPolicyIsStoredPerVehicle()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    GcsFailsafeCheck check(&bridge);
    clearGcsPolicy(check.id());

    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"),
                            QVariant(QStringLiteral("FixedWing"))));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_GCS_ENABL"), QVariant(0.0)));

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QCOMPARE(check.message(),
             QStringLiteral("GCS-failsafe policy not chosen for this airframe"));

    QSignalSpy policySpy(&check, &GcsFailsafeCheck::policyChanged);
    QVERIFY(check.setPolicy(QStringLiteral("telemetry_primary")));
    QCOMPARE(policySpy.count(), 1);
    QCOMPARE(check.policy(), QStringLiteral("telemetry_primary"));
    QCOMPARE(DatabaseManager::instance().getCheckConfig(check.id(), QStringLiteral("policy"), 0),
             QStringLiteral("telemetry_primary"));
    QCOMPARE(check.status(), CheckStatus::Failed);

    QVERIFY(!check.setPolicy(QStringLiteral("bogus")));
    QCOMPARE(check.policy(), QStringLiteral("telemetry_primary"));

    clearGcsPolicy(check.id());
}

// The GCS-failsafe policy belongs to one airframe. ArduPilot uses MAVLink
// system id 1 on every vehicle by default, so keying by the system id made a
// plane and a copter share one policy. With the hardware UID known, each
// airframe keeps its own.
void PreflightManagerTest::testGcsPolicyIsPerAirframeNotPerSystemId()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    GcsFailsafeCheck check(&bridge);
    QString uid = QStringLiteral("UID-PLANE");
    check.setDeviceUidProvider([&uid]() { return uid; });
    QVERIFY(check.setPolicy(QStringLiteral("rc_primary")));
    uid = QStringLiteral("UID-COPTER");                    // same system id, other airframe
    QVERIFY(check.setPolicy(QStringLiteral("telemetry_primary")));
    QCOMPARE(check.policy(), QStringLiteral("telemetry_primary"));
    uid = QStringLiteral("UID-PLANE");
    QCOMPARE(check.policy(), QStringLiteral("rc_primary"));   // the plane kept its own
    for (const char *u : {"UID-PLANE", "UID-COPTER"})
        DatabaseManager::instance().setCheckConfig(check.id(), QStringLiteral("policy@%1").arg(QLatin1String(u)),
                                                   QStringLiteral("unset"), -1);
}

// Only parameters on ParameterWatchlist reach the checks on a real vehicle;
// unit tests inject values directly and would not notice a missing entry. The
// GCS-failsafe check read FS_GCS_ENABL (Plane), which was not on the list.
void PreflightManagerTest::testSafetyCheckParametersAreWatched()
{
    const QSet<QString> watched = ParameterWatchlist::names();
    const QStringList needed = {
        QStringLiteral("FS_GCS_ENABL"), QStringLiteral("FS_GCS_ENABLE"), QStringLiteral("FS_GCS_TIMEOUT"),
        QStringLiteral("FS_LONG_TIMEOUT"), QStringLiteral("FS_LONG_ACTN"), QStringLiteral("FS_SHORT_ACTN"),
        QStringLiteral("FS_SHORT_TIMEOUT"), QStringLiteral("THR_FAILSAFE"), QStringLiteral("THR_FS_VALUE"),
        QStringLiteral("FS_THR_ENABLE"), QStringLiteral("FS_THR_VALUE"), QStringLiteral("RCMAP_THROTTLE"),
        QStringLiteral("RC3_MIN"), QStringLiteral("ARSPD_TYPE"), QStringLiteral("FLTMODE_CH"),
        QStringLiteral("BATT_CAPACITY"),
    };
    for (const QString &p : needed)
        QVERIFY2(watched.contains(p), qPrintable(p + QStringLiteral(" is read by a check but not watched")));
}

// ArduPlane's RC failsafe is THR_FAILSAFE (0 off, 1 on, 2 detect only), not
// ArduCopter's FS_THR_ENABLE: on the plane the old check was always Skipped.
void PreflightManagerTest::testRadioFailsafeReadsPlaneParameters()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    RadioFailsafeCheck check(&bridge);
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Skipped);                 // no RC failsafe parameters at all

    QVERIFY(setTestOverride(check, QStringLiteral("param_THR_FAILSAFE"), 1.0));
    QVERIFY(setTestOverride(check, QStringLiteral("param_THR_FS_VALUE"), 950.0));
    QVERIFY(setTestOverride(check, QStringLiteral("param_RC3_MIN"), 987.0));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_SHORT_ACTN"), 0.0));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_SHORT_TIMEOUT"), 1.5));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_LONG_ACTN"), 1.0));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_LONG_TIMEOUT"), 5.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QVERIFY2(check.message().contains(QStringLiteral("return to launch")), qPrintable(check.message()));

    QVERIFY(setTestOverride(check, QStringLiteral("param_THR_FS_VALUE"), 1000.0));   // at/above idle
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY2(check.message().contains(QStringLiteral("RC3_MIN")), qPrintable(check.message()));

    QVERIFY(setTestOverride(check, QStringLiteral("param_THR_FAILSAFE"), 0.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY(setTestOverride(check, QStringLiteral("param_THR_FAILSAFE"), 2.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY2(check.message().contains(QStringLiteral("no failsafe action")), qPrintable(check.message()));

    bool elrs = false;
    for (const QString &step : check.getFixSteps())
        elrs = elrs || step.contains(QStringLiteral("No pulses"));
    QVERIFY(elrs);
}

void PreflightManagerTest::testRadioFailsafeReadsCopterParameters()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    RadioFailsafeCheck check(&bridge);
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_THR_ENABLE"), 1.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QVERIFY2(check.message().contains(QStringLiteral("RTL")), qPrintable(check.message()));
    QVERIFY(setTestOverride(check, QStringLiteral("param_FS_THR_ENABLE"), 0.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Warning);
}

// A large roll at rest (the plane read about -46 deg on the bench) fails as
// before, and now says what to fix if the airframe is in fact level.
void PreflightManagerTest::testAttitudeFailurePointsToOrientation()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    AttitudeCheck check(&bridge, 30.0);
    QVERIFY(setTestOverride(check, QStringLiteral("roll"), qDegreesToRadians(-46.0)));
    QVERIFY(setTestOverride(check, QStringLiteral("pitch"), qDegreesToRadians(5.0)));
    QVERIFY(setTestOverride(check, QStringLiteral("yaw"), 0.0));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
    QVERIFY2(check.message().contains(QStringLiteral("AHRS_ORIENTATION")), qPrintable(check.message()));
    QVERIFY(check.getFixSteps().join(QStringLiteral(" ")).contains(QStringLiteral("Accelerometer")));
    QVERIFY(setTestOverride(check, QStringLiteral("roll"), qDegreesToRadians(29.0)));   // threshold unchanged
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
}

// ArduPlane marks the airspeed sensor (SYS_STATUS bit 0x10) healthy only when
// every configured sensor works; "configured but not found" must fail.
void PreflightManagerTest::testAirspeedSensorConfiguredButMissing()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);
    AirspeedSensorCheck check(&bridge);
    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"), QStringLiteral("Fixed wing")));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);                  // ARSPD_TYPE not loaded yet

    QVERIFY(setTestOverride(check, QStringLiteral("param_ARSPD_TYPE"), 1.0));   // MS4525 configured
    QVERIFY(setTestOverride(check, QStringLiteral("sensorHealth"), 0u));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);                  // no SYS_STATUS yet

    QVERIFY(setTestOverride(check, QStringLiteral("sensorHealth"), 0x0Fu));      // others fine, airspeed not
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
    QVERIFY2(check.message().contains(QStringLiteral("ARSPD_TYPE = 0")), qPrintable(check.message()));

    QVERIFY(setTestOverride(check, QStringLiteral("sensorHealth"), 0x1Fu));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);

    QVERIFY(setTestOverride(check, QStringLiteral("param_ARSPD_TYPE"), 0.0));
    QVERIFY(setTestOverride(check, QStringLiteral("sensorHealth"), 0x0Fu));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);                   // no sensor configured is fine

    QVERIFY(setTestOverride(check, QStringLiteral("vehicleType"), QStringLiteral("Quadrotor")));
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Skipped);
}

// Weather and airspace checks are informational: once the airframe is known
// they must stay non-mandatory, and even one marked mandatory by mistake must
// not hold the gate shut (allMandatoryPassed and armingBlocker skip them).
void PreflightManagerTest::testInformationalChecksNeverGateArming()
{
    PreflightManager mgr;
    QStringList informational;
    for (auto *c : mgr.checks()) {
        if (c->isInformational())
            informational << c->id();
    }
    QVERIFY2(informational.contains(QStringLiteral("env.temperature"))
                 && informational.contains(QStringLiteral("airspace.zone_compliance")),
             qPrintable(informational.join(QLatin1Char(','))));

    for (const QString &kind : {QStringLiteral("MULTIROTOR"), QStringLiteral("FIXED_WING")}) {
        mgr.applyVehicleKind(kind);
        for (auto *c : mgr.checks()) {
            if (c->isInformational())
                QVERIFY2(!c->mandatory(), qPrintable(kind + QStringLiteral(": ") + c->id()));
        }
    }

    // Leave only the informational checks, unevaluated or failing (no
    // telemetry, no zone model), and force them mandatory.
    bool someNotPassing = false;
    for (auto *c : mgr.checks()) {
        if (c->isInformational()) {
            c->setMandatory(true);
            const CheckStatus st = c->status();
            someNotPassing |= st == CheckStatus::Pending || st == CheckStatus::Error || st == CheckStatus::Failed;
        } else {
            c->setMandatory(false);
        }
    }
    QVERIFY2(someNotPassing, "the case needs an informational check that is not passing");
    QVERIFY(mgr.allMandatoryPassed());
    QVERIFY2(mgr.armingBlocker().isEmpty(), qPrintable(mgr.armingBlocker()));
}

// The exact set of checks that gate arming on each airframe. A change to the
// roster or to updateCriticalChecks() must update this list on purpose.
void PreflightManagerTest::testMandatorySetPerAirframe()
{
    const QStringList common = {
        QStringLiteral("airframe.imu.cal"), QStringLiteral("com.rc"), QStringLiteral("com.rc.failsafe"),
        QStringLiteral("com.rc.mode_switch"), QStringLiteral("com.telemetry.drop_rate"),
        QStringLiteral("comm.gcs.link_drops"), QStringLiteral("comm.heartbeat"),
        QStringLiteral("comm.mavlink.protocol"), QStringLiteral("comm.rc.rssi"),
        QStringLiteral("nav.airspeed.sensor"), QStringLiteral("nav.attitude"), QStringLiteral("nav.baro.health"),
        QStringLiteral("nav.gps.fix"), QStringLiteral("nav.home"), QStringLiteral("power.battery.voltage"),
        QStringLiteral("propulsion.esc.firmware"), QStringLiteral("propulsion.esc.responsiveness"),
        QStringLiteral("safety.failsafe.battery"), QStringLiteral("safety.failsafe.battery_param"),
        QStringLiteral("safety.failsafe.ekf"), QStringLiteral("safety.failsafe.gcs"),
        QStringLiteral("safety.failsafe.radio"),
    };
    auto mandatorySet = [](PreflightManager &mgr) {
        QStringList ids;
        for (auto *c : mgr.checks()) {
            if (c->mandatory())
                ids << c->id();
        }
        ids.sort();
        return ids;
    };

    PreflightManager mgr;
    QVERIFY(mandatorySet(mgr).isEmpty());   // nothing before the kind is known

    QStringList copter = common;
    copter << QStringLiteral("airframe.motor_count") << QStringLiteral("propulsion.motors.spin");
    copter.sort();
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    QCOMPARE(mandatorySet(mgr), copter);

    QStringList plane = common;
    plane << QStringLiteral("safety.rtl_alt");
    plane.sort();
    mgr.applyVehicleKind(QStringLiteral("FIXED_WING"));
    QCOMPARE(mandatorySet(mgr), plane);
}

UT_REGISTER_TEST(PreflightManagerTest)

#include "PreflightManagerTest.moc"
