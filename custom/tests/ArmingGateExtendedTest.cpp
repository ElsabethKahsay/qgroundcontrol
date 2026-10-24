#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"
#include "mocks/TestBlockingCheck.h"

class ArmingGateExtendedTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testForceArmNonExpiring();
    void testForceArmResetsCorrectly();
    void testInterceptCommandLongPassive();
    void testInterceptCommandLongActiveBlocks();
    void testInterceptCommandLongActiveAllowsWhenPassed();
    void testDenialReasonSignal();
    void testModeChange();
};

void ArmingGateExtendedTest::testForceArmNonExpiring()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    QSignalSpy allowedSpy(&gate, &ArmingGate::armingAllowedChanged);
    QSignalSpy overrideSpy(&gate, &ArmingGate::overrideActiveChanged);

    gate.forceArm();

    // The window is open for the minted token only, so a plain
    // arm — which is what armingAllowed answers — is still denied.
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());
    QCOMPARE(gate.overrideKind(), ArmingGate::ForceOverride);
    QVERIFY(!gate.denialReason().isEmpty());
    QVERIFY(overrideSpy.count() >= 1);
}

void ArmingGateExtendedTest::testForceArmResetsCorrectly()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);
    gate.forceArm();
    QVERIFY(gate.isOverrideActive());

    gate.resetGate();
    QVERIFY(!gate.isOverrideActive());
}

void ArmingGateExtendedTest::testInterceptCommandLongPassive()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Passive);

    QMap<int, float> params;
    params[1] = 1.0f; // arm param

    // Passive mode should always allow
    bool allowed = gate.interceptCommandLong(400, params);
    QVERIFY(allowed);
}

void ArmingGateExtendedTest::testInterceptCommandLongActiveBlocks()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    // Without a PreflightManager the inbound path used to ALLOW
    // every arm ("safety fallback"). It now fails closed like every other path.
    QMap<int, float> params;
    params[1] = 1.0f;
    bool allowed = gate.interceptCommandLong(400, params);
    QVERIFY(!allowed);
}

void ArmingGateExtendedTest::testInterceptCommandLongActiveAllowsWhenPassed()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);
    // A live autopilot link: no heartbeat is a hard block.
    MockTelemetryBridge bridge;
    gate.setTelemetryBridge(&bridge);
    bridge.emitHeartbeat();

    // Resolve the airframe kind so the mandatory set is configured;
    // the gate is fail-closed until the kind is known.
    mgr->applyVehicleKind(QStringLiteral("MULTIROTOR"));

    // Built-in Auto checks need live telemetry and refuse confirm(); exercise
    // the allow path with one explicit mandatory check instead.
    for (auto *c : mgr->checks()) {
        c->setMandatory(false);
    }
    TestBlockingCheck *passCheck = new TestBlockingCheck(QStringLiteral("test.passing"));
    passCheck->forceStatus(CheckStatus::Passed, QStringLiteral("Healthy"));
    mgr->addCheck(passCheck);
    QMap<int, float> params;
    params[1] = 1.0f;
    bool allowed = gate.interceptCommandLong(400, params);
    QVERIFY(allowed);

    delete mgr;
}

void ArmingGateExtendedTest::testDenialReasonSignal()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    QSignalSpy denialSpy(&gate, &ArmingGate::denialReasonChanged);

    // Any evaluation of a gate with no telemetry denies with a reason
    // (decide() is the only evaluation path).
    gate.resetGate();
    QVERIFY(!gate.denialReason().isEmpty());
    QVERIFY(denialSpy.count() >= 1);
}

void ArmingGateExtendedTest::testModeChange()
{
    ArmingGate gate;
    QSignalSpy modeSpy(&gate, &ArmingGate::modeChanged);

    gate.setMode(ArmingGate::Hybrid);
    QCOMPARE(gate.mode(), ArmingGate::Hybrid);
    QVERIFY(modeSpy.count() == 1);

    // Setting same mode should not emit
    gate.setMode(ArmingGate::Hybrid);
    QVERIFY(modeSpy.count() == 1);
}

UT_REGISTER_TEST(ArmingGateExtendedTest)

#include "ArmingGateExtendedTest.moc"
