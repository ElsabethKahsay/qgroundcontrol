#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "AbstractCheck.h"
#include "mocks/MockTelemetryBridge.h"

class TestArmCheck : public AbstractCheck {
    Q_OBJECT
public:
    TestArmCheck(const QString &id, CheckType type = CheckType::Auto,
                 bool mandatory = true, QObject *parent = nullptr)
        : AbstractCheck(id, QStringLiteral("Test ") + id,
                        CheckCategory::Safety, type, mandatory, true, parent)
    {}
    void evaluate() override {}
    void forceStatus(CheckStatus s, const QString &msg = {}) { setStatus(s, msg); }
};

class ArmingGateTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testPassiveMode();
    void testActiveModeBlocksOnFail();
    void testActiveModeAllowsOnPass();
    void testHybridModeOverride();
    void testHybridModeOverrideLogged();
    void testDisconnectResetsGate();
};

void ArmingGateTest::testPassiveMode()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Passive);
    QCOMPARE(gate.mode(), ArmingGate::Passive);
    // Passive mode = log only, the gate is always open
    QVERIFY(gate.isArmingAllowed());

    QCOMPARE(gate.processArmRequest(5, 0), ArmingGate::GATE_OPEN);
    QVERIFY(gate.isArmingAllowed());
}

void ArmingGateTest::testActiveModeBlocksOnFail()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    ArmingGate::GateDecision decision = gate.processArmRequest(1, 0);
    QCOMPARE(decision, ArmingGate::GATE_CLOSED);
    QVERIFY(!gate.isArmingAllowed());
    QVERIFY(!gate.denialReason().isEmpty());
}

void ArmingGateTest::testActiveModeAllowsOnPass()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);

    ArmingGate::GateDecision decision = gate.processArmRequest(0, 0);
    QCOMPARE(decision, ArmingGate::GATE_OPEN);
    QVERIFY(gate.isArmingAllowed());
}

void ArmingGateTest::testHybridModeOverride()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);

    QVERIFY(!gate.isArmingAllowed());
    QCOMPARE(gate.processArmRequest(1, 0), ArmingGate::GATE_CLOSED);

    // Acknowledging is a one-time ALLOW_WITH_ACK bypass, not a persistent override
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("TestPilot"), QStringLiteral("Test override")));
    QCOMPARE(gate.processArmRequest(1, 0), ArmingGate::GATE_OPEN);
    QCOMPARE(gate.processArmRequest(1, 0), ArmingGate::GATE_CLOSED);
}

void ArmingGateTest::testHybridModeOverrideLogged()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Hybrid);
    gate.processArmRequest(1, 0);

    // overrideGate activates + logs a timed override for the audit trail
    QSignalSpy overrideSpy(&gate, &ArmingGate::gateOverrideLogged);
    gate.overrideGate(QStringLiteral("Test"), 30);
    QCOMPARE(overrideSpy.count(), 1);
}

void ArmingGateTest::testDisconnectResetsGate()
{
    ArmingGate gate;
    gate.setMode(ArmingGate::Active);
    gate.processArmRequest(0, 0);
    QVERIFY(gate.isArmingAllowed());

    gate.resetGate();
    QVERIFY(!gate.isArmingAllowed());
}

UT_REGISTER_TEST(ArmingGateTest)

#include "ArmingGateTest.moc"
