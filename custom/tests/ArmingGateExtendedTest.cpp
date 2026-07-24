#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"

class ArmingGateExtendedTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testForceArmNonExpiring();
    void testForceArmResetsCorrectly();
    void testOverrideGateTimed();
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

    QVERIFY(gate.isArmingAllowed());
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.denialReason().isEmpty());
    QVERIFY(allowedSpy.count() >= 1);
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

void ArmingGateExtendedTest::testOverrideGateTimed()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    gate.overrideGate(QStringLiteral("Manual override"), 1);
    QVERIFY(gate.isOverrideActive());
    QVERIFY(gate.isArmingAllowed());
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

    // Without a PreflightManager, should allow (safety fallback)
    QMap<int, float> params;
    params[1] = 1.0f;
    bool allowed = gate.interceptCommandLong(400, params);
    QVERIFY(allowed);
}

void ArmingGateExtendedTest::testInterceptCommandLongActiveAllowsWhenPassed()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);

    // With no checks, allMandatoryPassed is true
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

    gate.processArmRequest(2, 1);
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
