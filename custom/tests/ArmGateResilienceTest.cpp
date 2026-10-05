// Adversarial tests for the arm gate's safety logic.
//
// Arming is the one irreversible thing this application can cause, so its
// fail-open paths matter more than its convenience paths. The properties pinned
// here are the ones a flaky radio or a recycled link can attack:
//
//   * a healthy link must not be blocked by the staleness guard;
//   * a link that has never produced telemetry must be blocked;
//   * a force-arm override is a one-shot, caller-scoped token, not a window that
//     lets any arm through;
//   * losing the gate mid-override must deny, not crash and not allow.
//
// The staleness tests below were written after finding that ArmingGate's
// "telemetry stale" clock is fed by PreflightManager::progressChanged, which is
// emitted on check-status changes rather than on vehicle telemetry. See the
// comments on the individual cases for what that implies.

#include <QSignalSpy>
#include <QTest>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"
#include "mocks/TestBlockingCheck.h"

class ArmGateResilienceTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override;
    void cleanup() override;

    void testStaleAfterSilenceFollowingRealTelemetry();
    void testNeverSawTelemetryIsFailClosed();
    void testForceArmTokenIsOneShot();
    void testForceArmWindowIsNotCallerAmbient();
    void testNewTokenInvalidatesPrevious();
    void testTokenDeniedWhenGateDestroyed();
    void testSpentTokenCannotBeReplayedAfterWindowCloses();
    void testZeroTokenNeverAuthorizes();

private:
    /// Builds a gate with one mandatory passing check and a healthy bridge.
    void _buildArmableGate(bool withTelemetry = true);

    /// Marks every check in the manager as Passed.
    void _passAllChecks();

    /// Provokes exactly one PreflightManager::progressChanged emission.
    void _deliverTick();

    /// Waits for ArmingGate's evaluation timer to run at least twice.
    /// ArmingGate only recomputes armingAllowed from check results and telemetry
    /// freshness inside updateArmingState(), which runs on a 500 ms timer started
    /// by setPreflightManager(). Before the first tick, armingAllowed is still
    /// its constructed default (false) with an empty reason, so asserting
    /// immediately after setup measures nothing. Everything below waits here
    /// first; nothing asserts against a pre-evaluation gate.
    void _settle();

    MockTelemetryBridge *_bridge  = nullptr;
    PreflightManager    *_manager = nullptr;
    ArmingGate          *_gate    = nullptr;
    TestBlockingCheck   *_check   = nullptr;
};

void ArmGateResilienceTest::init()
{
    UnitTest::init();
}

void ArmGateResilienceTest::cleanup()
{
    UnitTest::cleanup();
}

void ArmGateResilienceTest::_passAllChecks()
{
    const QVector<AbstractCheck *> all = _manager->checks();
    for (AbstractCheck *check : all) {
        if (check != _check) {
            check->setMandatory(false);
        }
    }
    _check->setMandatory(true);
    _check->forceStatus(CheckStatus::Passed);

    qInfo() << "ArmGateResilience:" << all.size() << "checks present, mandatoryPassed ="
            << _manager->allMandatoryPassed() << "blocker =" << _manager->armingBlocker();
}

void ArmGateResilienceTest::_settle()
{
    // kArmingGateEvalMs is 500 ms in custom/src/utils/Config.h; two ticks plus
    // margin so the state under test is the timer's, not a leftover.
    for (int i = 0; i < 3; ++i) {
        QTest::qWait(600);
    }
}

// ArmingGate's freshness clock is driven by PreflightManager::progressChanged,
// so a "telemetry tick" for this gate means a check changing state. That is a
// narrower thing than vehicle telemetry: mutating MockTelemetryBridge does not
// move this clock at all. Modelling the tick explicitly is what lets the two
// cases below be distinguished - a gate that has seen one tick, and a gate that
// has never seen any.
void ArmGateResilienceTest::_deliverTick()
{
    _check->forceStatus(CheckStatus::Failed, QStringLiteral("tick"));
    _check->forceStatus(CheckStatus::Passed, QString());
}

void ArmGateResilienceTest::_buildArmableGate(bool withTelemetry)
{
    _bridge  = new MockTelemetryBridge(this);
    _manager = new PreflightManager(1, this);
    _gate    = new ArmingGate(this);
    _check   = new TestBlockingCheck(QStringLiteral("resilience.check"));

    _bridge->setConnected(true);
    _bridge->setConnectionQuality(100);
    _bridge->setArmed(false);

    _manager->setTelemetryBridge(_bridge);
    _manager->addCheck(_check);
    // Resolve the airframe kind before anything else. allMandatoryPassed() fails
    // closed while m_kindResolved is false (R16: the applicable mandatory set is
    // undefined until the vehicle kind is known), so without this the gate is
    // blocked for a reason unrelated to anything under test.
    _manager->applyVehicleKind(QStringLiteral("MULTIROTOR"));
    // Reduce enforcement to exactly one check this harness controls.
    // applyVehicleKind() configures the applicable mandatory set for the
    // airframe and runs evaluateAll(), which populates checks a bare harness
    // has no data for - zone compliance reports "Zone model unavailable" and
    // blocks arming for reasons unrelated to anything under test. Both cases
    // below are about the freshness clock, so the baseline has to be
    // unambiguously "the one mandatory check passes".
    //
    // overrideStatus() cannot be used for this: it refuses Auto checks, and
    // every auto-added check is Auto. setMandatory() is unconditional.
    _passAllChecks();

    _gate->setMode(ArmingGate::Active);
    _gate->setPreflightManager(_manager);
    _gate->setTelemetryBridge(_bridge);

    if (withTelemetry) {
        _deliverTick();
    }
}

void ArmGateResilienceTest::testStaleAfterSilenceFollowingRealTelemetry()
{
    // One real tick, so the gate has evidence of a live link.
    _buildArmableGate();
    _settle();

    QVERIFY2(_manager->allMandatoryPassed(),
             qPrintable(QStringLiteral("baseline is not armable: %1").arg(_manager->armingBlocker())));
    QVERIFY2(_gate->isArmingAllowed(),
             qPrintable(QStringLiteral("gate blocked despite a fresh tick: %1").arg(_gate->denialReason())));

    // Now go silent: no further progressChanged, so the staleness clock runs out.
    // Config.h sets kTelemetryStalenessSec = 10 and ArmingGate re-evaluates on a
    // timer, so the gate must flip to denied on its own.
    const int quietSecs = 14;
    for (int i = 0; i < quietSecs; ++i) {
        QTest::qWait(1000);
        // Bridge updates are NOT ticks for this clock. They are made here
        // deliberately, to pin down that only progressChanged moves the gate.
        _bridge->setBatteryPercent(50 + (i % 5));
        _bridge->setConnectionQuality(95);
    }

    const bool allowed = _gate->isArmingAllowed();
    const QString reason = _gate->denialReason();
    qInfo() << "ArmGateResilience: after" << quietSecs << "s of silence, armingAllowed =" << allowed
            << "reason =" << reason;

    QVERIFY2(!allowed,
             qPrintable(QStringLiteral(
                 "Gate stayed open %1s after the last telemetry tick. The staleness "
                 "guard stopped working. reason=\\\"%2\\\"")
                            .arg(quietSecs)
                            .arg(reason)));
    QVERIFY2(reason.contains(QStringLiteral("stale"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral(
                 "Gate closed, but not for staleness - reason was \"%1\". A silent "
                 "link must be reported as stale so the operator can tell a quiet "
                 "vehicle from a blocked check.")
                            .arg(reason)));
}

void ArmGateResilienceTest::testNeverSawTelemetryIsFailClosed()
{
    // Build the same healthy, all-checks-passing gate, but never deliver a tick.
    _buildArmableGate(false);
    _settle();

    const bool allowed = _gate->isArmingAllowed();
    const QString reason = _gate->denialReason();
    qInfo() << "ArmGateResilience: no telemetry ever seen, armingAllowed =" << allowed
            << "reason =" << reason;

    // Anti-vacuity: all checks pass, so the ONLY thing that can close this gate
    // is the freshness guard. Asserting the reason keeps this from silently
    // becoming a test of some unrelated blocker.
    QVERIFY2(_manager->allMandatoryPassed(),
             qPrintable(QStringLiteral(
                 "baseline checks are not passing, so this case cannot distinguish "
                 "fail-open from fail-closed: %1")
                            .arg(_manager->armingBlocker())));

    QVERIFY2(!allowed,
             "Gate allowed arming despite never having received any telemetry. "
             "\"No data yet\" must be treated as stale, not as fresh.");
    QVERIFY2(reason.contains(QStringLiteral("No telemetry"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral(
                 "Gate closed, but not for want of telemetry - reason was \"%1\".")
                            .arg(reason)));
}

void ArmGateResilienceTest::testForceArmTokenIsOneShot()
{
    _buildArmableGate();
    _gate->setMode(ArmingGate::Active);

    const auto interceptor = _gate->makeArmInterceptor();

    QString reason;
    const quint32 token = _gate->forceArm();
    QVERIFY(token != 0);

    QVERIFY2(interceptor(reason, token), qPrintable(QStringLiteral("first use rejected: %1").arg(reason)));

    // Replaying the identical token must fail: forceArm mints a one-shot token
    // and consumes it on success, so a duplicate or retried arm command cannot
    // ride the same authorisation.
    QVERIFY2(!interceptor(reason, token),
             "token was accepted twice; the force-arm override is not one-shot");
}

void ArmGateResilienceTest::testForceArmWindowIsNotCallerAmbient()
{
    _buildArmableGate();

    const auto interceptor = _gate->makeArmInterceptor();
    QString reason;

    const quint32 token = _gate->forceArm();
    QVERIFY(token != 0);

    // An arm command that does NOT carry the minted token must be denied while
    // the force-arm window is open. If this passes, any concurrent arm command
    // (a stale retry, another component, a second UI) is authorised by an
    // operator action that was aimed at one specific command.
    QVERIFY2(!interceptor(reason, 0),
             "arm without the token was authorised inside a force-arm window");
    QVERIFY2(!interceptor(reason, token + 1),
             "arm with a wrong token was authorised inside a force-arm window");

    // And the genuine holder still works afterwards, i.e. the denials above did
    // not consume the token.
    QVERIFY2(interceptor(reason, token),
             qPrintable(QStringLiteral("real token rejected after bogus attempts: %1").arg(reason)));
}

void ArmGateResilienceTest::testNewTokenInvalidatesPrevious()
{
    _buildArmableGate();
    const auto interceptor = _gate->makeArmInterceptor();
    QString reason;

    const quint32 first = _gate->forceArm();
    const quint32 second = _gate->forceArm();
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QVERIFY(second != first);

    // Only the most recently minted token is live; the superseded one must not
    // still authorise an arm.
    QVERIFY2(!interceptor(reason, first),
             "a superseded force-arm token still authorised an arm command");
    QVERIFY2(interceptor(reason, second),
             qPrintable(QStringLiteral("current token rejected: %1").arg(reason)));
}

void ArmGateResilienceTest::testTokenDeniedWhenGateDestroyed()
{
    _buildArmableGate();
    const auto interceptor = _gate->makeArmInterceptor();
    QString reason;

    const quint32 token = _gate->forceArm();
    QVERIFY(token != 0);

    // The Vehicle holds only the callable, not the gate, so the gate can be torn
    // down while a command is in flight (page closed, plugin unloaded, shutdown).
    // The QPointer guard in the lambda must make this fail closed rather than
    // dereference freed memory.
    delete _gate;
    _gate = nullptr;

    QVERIFY2(!interceptor(reason, token),
             "arm was authorised after the gate was destroyed; this must fail closed");
    QVERIFY(!reason.isEmpty());
}

void ArmGateResilienceTest::testSpentTokenCannotBeReplayedAfterWindowCloses()
{
    _buildArmableGate();
    const auto interceptor = _gate->makeArmInterceptor();
    QString reason;

    const quint32 token = _gate->forceArm();
    QVERIFY2(interceptor(reason, token), "the minted token should authorise exactly one arm");

    // Close the override window, then SHUT the gate. Shutting it matters: with a
    // healthy gate the interceptor is supposed to return true because arming is
    // genuinely allowed, which would hide whether the spent token was reused.
    // With the gate closed, the token is the only possible way through.
    _gate->resetGate();
    _check->forceStatus(CheckStatus::Failed, QStringLiteral("gate closed after override"));
    _settle();
    QVERIFY(!_gate->isArmingAllowed());

    QVERIFY2(!interceptor(reason, token),
             "a spent force-arm token still authorised an arm after the window closed and the "
             "gate was shut; it must not become a standing authorisation");
}

void ArmGateResilienceTest::testZeroTokenNeverAuthorizes()
{
    _buildArmableGate();
    // Force the gate shut first so the only way to pass is an override.
    _settle();
    _check->forceStatus(CheckStatus::Failed, QStringLiteral("adversarial block"));
    _settle();
    QVERIFY(!_gate->isArmingAllowed());

    const auto interceptor = _gate->makeArmInterceptor();
    QString reason;

    // forceArm() increments a quint32 sequence counter, so after 2^32 calls the
    // minted token would wrap to 0. The interceptor guards on m_overrideToken != 0
    // before comparing, which is what makes that safe. Pin the behaviour so a
    // future refactor cannot quietly turn a wrapped token into a valid one.
    _gate->forceArm();
    QVERIFY(!interceptor(reason, 0));
    QVERIFY(!reason.isEmpty());
}

UT_REGISTER_TEST(ArmGateResilienceTest)

#include "ArmGateResilienceTest.moc"
