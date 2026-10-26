#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>

#include "UnitTest.h"
#include "ArmingGate.h"
#include "AbstractCheck.h"
#include "core/BatteryPackCheck.h"
#include "PreflightManager.h"
#include "VehicleProfileManager.h"
#include "utils/DatabaseManager.h"
#include "mocks/MockTelemetryBridge.h"

// ArmingGate::decide() is the single arm decision.
// Most rules are tested on the pure function; gate-level behaviour (force
// window, overrideKind, inbound peer arms, token reuse) on a bare ArmingGate.
class ArmingGateTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testDisarmAlwaysAllowed();
    void testTrainingBlocksGcsArm();
    void testHardBlocksNotOverridableByAck();
    void testAckAllowsOnlyItsTokenAndItsChecks();
    void testAllPassedAllows();
    void testForceWindowOnlyForMintedToken();
    void testPassiveModeAllows();
    void testArmingAllowedFalseDuringForceWindow();
    void testForceWindowClosesOnUse();
    void testInboundPeerArmNeverSpendsAck();
    void testTokensNeverRepeatAfterReset();
    void testNoBatteryPackIsHardBlock();
    void testBatteryPackCheckFollowsSelection();
    void testBatteryPackWaiverAllowsArmWithoutEstimate();
    void testBatteryPackWarnsOnFcCapacityMismatch();

private:
    static ArmingGate::ArmRequest freshFailing(const QStringList &failing);
};

ArmingGate::ArmRequest ArmingGateTest::freshFailing(const QStringList &failing)
{
    ArmingGate::ArmRequest r;
    r.heartbeatAgeMs = 500;
    r.allMandatoryPassed = failing.isEmpty();
    r.failingMandatoryIds = failing;
    r.blocker = failing.isEmpty() ? QString() : failing.first() + QStringLiteral(" failed");
    return r;
}

// Disarm passes in every gate state.
void ArmingGateTest::testDisarmAlwaysAllowed()
{
    ArmingGate::GateSnapshot g;
    g.forceWindowOpen = true;
    g.forceToken = 7;
    ArmingGate::ArmRequest r;          // no heartbeat ever, nothing passed
    r.isArm = false;
    r.training = true;
    r.anyMandatoryStale = true;
    r.batteryPackSelected = false;
    QCOMPARE(ArmingGate::decide(r, g).verdict, ArmingGate::ArmDecision::Allow);
}

// Training blocks the GCS's own arm, even with everything green.
void ArmingGateTest::testTrainingBlocksGcsArm()
{
    ArmingGate::ArmRequest r = freshFailing({});
    r.training = true;
    const auto d = ArmingGate::decide(r, ArmingGate::GateSnapshot{});
    QCOMPARE(d.verdict, ArmingGate::ArmDecision::Deny);
    QVERIFY(d.reason.contains(QStringLiteral("training"), Qt::CaseInsensitive));
}

// No heartbeat, a stale heartbeat, or a Stale mandatory check
// deny even with a valid, matching acknowledgment.
void ArmingGateTest::testHardBlocksNotOverridableByAck()
{
    ArmingGate::GateSnapshot g;
    g.ackValid = true;
    g.ackToken = 42;
    g.ackCheckIds = {QStringLiteral("nav.gps.fix")};

    ArmingGate::ArmRequest r = freshFailing({QStringLiteral("nav.gps.fix")});
    r.callToken = 42;
    QCOMPARE(ArmingGate::decide(r, g).verdict, ArmingGate::ArmDecision::Allow);   // baseline: ack works

    ArmingGate::ArmRequest noHb = r;
    noHb.heartbeatAgeMs = -1;
    QCOMPARE(ArmingGate::decide(noHb, g).verdict, ArmingGate::ArmDecision::Deny);

    ArmingGate::ArmRequest staleHb = r;
    staleHb.heartbeatAgeMs = (kTelemetryStalenessSec + 1) * 1000;
    QCOMPARE(ArmingGate::decide(staleHb, g).verdict, ArmingGate::ArmDecision::Deny);

    ArmingGate::ArmRequest staleCheck = r;
    staleCheck.anyMandatoryStale = true;
    QCOMPARE(ArmingGate::decide(staleCheck, g).verdict, ArmingGate::ArmDecision::Deny);
}

// Only the ack's own token spends it, and only while every
// failing check is one the pilot acknowledged.
void ArmingGateTest::testAckAllowsOnlyItsTokenAndItsChecks()
{
    ArmingGate::GateSnapshot g;
    g.ackValid = true;
    g.ackToken = 42;
    g.ackCheckIds = {QStringLiteral("nav.gps.fix")};

    ArmingGate::ArmRequest r = freshFailing({QStringLiteral("nav.gps.fix")});
    QCOMPARE(ArmingGate::decide(r, g).verdict, ArmingGate::ArmDecision::NeedsAck);   // stock arm, token 0

    r.callToken = 42;
    const auto ok = ArmingGate::decide(r, g);
    QCOMPARE(ok.verdict, ArmingGate::ArmDecision::Allow);
    QCOMPARE(ok.kind, ArmingGate::ArmDecision::Ack);

    ArmingGate::ArmRequest newFailure = freshFailing({QStringLiteral("nav.gps.fix"), QStringLiteral("power.battery.voltage")});
    newFailure.callToken = 42;
    const auto voided = ArmingGate::decide(newFailure, g);
    QCOMPARE(voided.verdict, ArmingGate::ArmDecision::Deny);
    QVERIFY(voided.reason.contains(QStringLiteral("power.battery.voltage")));

    ArmingGate::GateSnapshot noAck;     // no ack at all: failing → NeedsAck
    QCOMPARE(ArmingGate::decide(freshFailing({QStringLiteral("x")}), noAck).verdict,
             ArmingGate::ArmDecision::NeedsAck);
}

void ArmingGateTest::testAllPassedAllows()
{
    const auto d = ArmingGate::decide(freshFailing({}), ArmingGate::GateSnapshot{});
    QCOMPARE(d.verdict, ArmingGate::ArmDecision::Allow);
    QCOMPARE(d.kind, ArmingGate::ArmDecision::Normal);
}

// Inside a force window only the minted token passes; a plain arm (token
// 0, i.e. armingAllowed) is denied even with every check green.
void ArmingGateTest::testForceWindowOnlyForMintedToken()
{
    ArmingGate::GateSnapshot g;
    g.forceWindowOpen = true;
    g.forceToken = 9;
    ArmingGate::ArmRequest r = freshFailing({});
    QCOMPARE(ArmingGate::decide(r, g).verdict, ArmingGate::ArmDecision::Deny);
    r.callToken = 9;
    const auto d = ArmingGate::decide(r, g);
    QCOMPARE(d.verdict, ArmingGate::ArmDecision::Allow);
    QCOMPARE(d.kind, ArmingGate::ArmDecision::Force);
}

// Passive is test-only now (QML cannot set the mode) and still allows.
void ArmingGateTest::testPassiveModeAllows()
{
    ArmingGate::GateSnapshot g;
    g.mode = ArmingGate::Passive;
    QCOMPARE(ArmingGate::decide(ArmingGate::ArmRequest{}, g).verdict, ArmingGate::ArmDecision::Allow);
}

// Same rule at gate level: armingAllowed is false inside a force window.
void ArmingGateTest::testArmingAllowedFalseDuringForceWindow()
{
    ArmingGate gate;
    QCOMPARE(gate.overrideKind(), ArmingGate::NoOverride);
    gate.forceArm();
    QCOMPARE(gate.overrideKind(), ArmingGate::ForceOverride);
    QVERIFY(gate.isOverrideActive());
    QVERIFY2(!gate.isArmingAllowed(), "armingAllowed must answer for a plain arm, which a force window denies");
}

// The force window closes as soon as its token is used.
void ArmingGateTest::testForceWindowClosesOnUse()
{
    ArmingGate gate;
    const quint32 token = gate.forceArm();
    auto interceptor = gate.makeArmInterceptor();
    QString reason;
    QVERIFY2(interceptor(reason, token), qPrintable(reason));
    QCOMPARE(gate.overrideKind(), ArmingGate::NoOverride);
    QVERIFY(!gate.isOverrideActive());
    QVERIFY(!interceptor(reason, token));   // spent: and the gate is otherwise closed
}

// A peer's inbound arm is judged read-only with token 0; it can
// never spend the operator's acknowledgment.
void ArmingGateTest::testInboundPeerArmNeverSpendsAck()
{
    ArmingGate gate;
    MockTelemetryBridge bridge;      // live link, so only the token decides
    gate.setTelemetryBridge(&bridge);
    bridge.emitHeartbeat();
    QVERIFY(gate.acknowledgeOverride(QStringLiteral("Pilot"), QStringLiteral("Test")));
    const quint32 ack = gate.ackTokenForArm();
    QVERIFY(ack != 0);

    QMap<int, float> arm;
    arm.insert(1, 1.0f);
    QVERIFY(!gate.interceptCommandLong(MAV_CMD_COMPONENT_ARM_DISARM, arm));
    QCOMPARE(gate.ackTokenForArm(), ack);
    QCOMPARE(gate.overrideKind(), ArmingGate::AckOverride);
}

// resetGate() no longer resets the token counter, so a force token from an
// earlier window can never match a later one.
void ArmingGateTest::testTokensNeverRepeatAfterReset()
{
    ArmingGate gate;
    const quint32 first = gate.forceArm();
    gate.resetGate();
    const quint32 second = gate.forceArm();
    QVERIFY(second != first);
}

// No pack selected denies even with a valid ack, in Flight and Testing
// alike, and a bench force token does not get past it either.
void ArmingGateTest::testNoBatteryPackIsHardBlock()
{
    ArmingGate::GateSnapshot g;
    g.ackValid = true;
    g.ackToken = 42;
    g.ackCheckIds = {QStringLiteral("power.battery.pack")};
    ArmingGate::ArmRequest r = freshFailing({QStringLiteral("power.battery.pack")});
    r.callToken = 42;
    r.batteryPackSelected = false;
    const auto d = ArmingGate::decide(r, g);
    QCOMPARE(d.verdict, ArmingGate::ArmDecision::Deny);
    QVERIFY(d.reason.contains(QStringLiteral("battery pack"), Qt::CaseInsensitive));

    ArmingGate::GateSnapshot force;
    force.forceWindowOpen = true;
    force.forceToken = 9;
    r.callToken = 9;
    const auto f = ArmingGate::decide(r, force);
    QCOMPARE(f.verdict, ArmingGate::ArmDecision::Deny);
    QVERIFY2(f.reason.contains(QStringLiteral("battery pack"), Qt::CaseInsensitive), qPrintable(f.reason));

    // With the pack chosen (or waived) the same force token passes.
    r.batteryPackSelected = true;
    QCOMPARE(ArmingGate::decide(r, force).verdict, ArmingGate::ArmDecision::Allow);
}

void ArmingGateTest::testBatteryPackCheckFollowsSelection()
{
    QVariantMap pack;
    BatteryPackCheck check([&pack]() { return pack; });
    QCOMPARE(check.status(), CheckStatus::Pending);
    QVERIFY(check.mandatory());

    pack = {{QStringLiteral("serial"), QStringLiteral("PK-07")},
            {QStringLiteral("label"), QStringLiteral("Wing A")},
            {QStringLiteral("ratedMah"), 6000}, {QStringLiteral("cycles"), 3}};
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QVERIFY(check.message().contains(QStringLiteral("3 cycles")));
    check.reset();                                    // a selection survives a checklist reset
    QCOMPARE(check.status(), CheckStatus::Passed);

    pack.clear();
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Pending);
    // A generic "Run"/confirm button cannot pass it without a pack.
    check.confirm(QStringLiteral("Action completed"));
    QCOMPARE(check.status(), CheckStatus::Pending);

    QVERIFY(!batteryFlightCountsAsCycle(1199.0, 6000));
    QVERIFY(batteryFlightCountsAsCycle(1200.0, 6000));
    QVERIFY(!batteryFlightCountsAsCycle(5000.0, 0));
}

// The pilot can waive the pack for one flight. The waiver needs a name, is
// logged, turns the "Battery pack" item into a warning (so the gate no longer
// sees a missing pack), and is dropped by choosing a pack or by
// clearBatteryPackWaiver() (disarm, vehicle change).
void ArmingGateTest::testBatteryPackWaiverAllowsArmWithoutEstimate()
{
    DatabaseManager &db = DatabaseManager::instance();
    db.reset();
    QVERIFY(db.initialize());
    const QStringList before = db.listComplianceLogs(QStringLiteral("unknown"), 100000);

    VehicleProfileManager vpm;
    // The same provider shape PreflightPlugin gives the check.
    auto *pack = new BatteryPackCheck([&vpm]() -> QVariantMap {
        if (!vpm.currentBatterySerial().isEmpty())
            return {{QStringLiteral("serial"), vpm.currentBatterySerial()}};
        if (vpm.batteryPackWaived())
            return {{QStringLiteral("waivedBy"), vpm.batteryPackWaivedBy()}};
        return {};
    });
    QObject::connect(&vpm, &VehicleProfileManager::currentBatteryChanged, pack, [pack]() { pack->evaluate(); });

    ArmingGate gate;
    PreflightManager *mgr = new PreflightManager(1, &gate);
    gate.setPreflightManager(mgr);
    for (auto *c : mgr->checks())
        c->setMandatory(false);
    mgr->addCheck(pack);
    pack->setMandatory(true);

    QCOMPARE(pack->status(), CheckStatus::Pending);
    QVERIFY(!gate.currentRequest(0).batteryPackSelected);

    QVERIFY(!vpm.waiveBatteryPack(QStringLiteral("   ")));   // a name is required
    QVERIFY(!vpm.batteryPackWaived());
    QVERIFY(!gate.currentRequest(0).batteryPackSelected);

    QVERIFY(vpm.waiveBatteryPack(QStringLiteral("Waiver Pilot")));
    QVERIFY(vpm.batteryPackWaived());
    QCOMPARE(vpm.batteryPackWaivedBy(), QStringLiteral("Waiver Pilot"));
    QCOMPARE(pack->status(), CheckStatus::Warning);
    QVERIFY2(pack->message().contains(QStringLiteral("Waiver Pilot"))
                 && pack->message().contains(QStringLiteral("no flight-time estimate")),
             qPrintable(pack->message()));
    QVERIFY(gate.currentRequest(0).batteryPackSelected);

    bool logged = false;
    for (const QString &id : db.listComplianceLogs(QStringLiteral("unknown"), 100000)) {
        if (before.contains(id))
            continue;
        const QJsonObject e = QJsonDocument::fromJson(db.loadComplianceLog(id).toUtf8()).object();
        logged |= e.value(QStringLiteral("event")).toString() == QLatin1String("battery_pack_waived")
                  && e.value(QStringLiteral("pilot")).toString() == QLatin1String("Waiver Pilot");
    }
    QVERIFY2(logged, "the pack waiver was not written to compliance_logs");

    // Disarm (or a vehicle change) drops it: the next flight needs a pack again.
    vpm.clearBatteryPackWaiver();
    QVERIFY(!vpm.batteryPackWaived());
    QCOMPARE(pack->status(), CheckStatus::Pending);
    QVERIFY(!gate.currentRequest(0).batteryPackSelected);

    // Choosing a pack also replaces a waiver.
    QVERIFY(vpm.waiveBatteryPack(QStringLiteral("Waiver Pilot")));
    vpm.setBatterySerial(QStringLiteral("PK-WAIVER-TEST"));
    QVERIFY(!vpm.batteryPackWaived());
    QCOMPARE(pack->status(), CheckStatus::Passed);

    delete mgr;
}

// The FC's BATT_CAPACITY must match the fitted pack, or its % and battery
// failsafes are wrong: a Warning (not a block), with the value to set.
void ArmingGateTest::testBatteryPackWarnsOnFcCapacityMismatch()
{
    QVariantMap pack{{QStringLiteral("serial"), QStringLiteral("PK-01")},
                     {QStringLiteral("ratedMah"), 5000}, {QStringLiteral("cycles"), 0},
                     {QStringLiteral("fcCapacityMah"), 3300}};
    BatteryPackCheck check([&pack]() { return pack; });
    QCOMPARE(check.status(), CheckStatus::Warning);
    QVERIFY2(check.message().contains(QStringLiteral("BATT_CAPACITY = 5000")), qPrintable(check.message()));

    pack[QStringLiteral("fcCapacityMah")] = 5100;      // within 5 %
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    pack.remove(QStringLiteral("fcCapacityMah"));       // parameter not loaded yet
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);

    // A Warning still counts as a selected pack for the arming gate.
    pack[QStringLiteral("fcCapacityMah")] = 3300;
    check.evaluate();
    QVERIFY(check.status() == CheckStatus::Warning);
}

UT_REGISTER_TEST(ArmingGateTest)

#include "ArmingGateTest.moc"
