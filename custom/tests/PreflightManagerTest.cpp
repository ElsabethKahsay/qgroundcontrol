#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "mocks/MockTelemetryBridge.h"

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
};

void PreflightManagerTest::testInitialState()
{
    PreflightManager mgr;
    // Built-in phase-1 checks are auto-registered at construction
    QVERIFY(mgr.totalChecks() > 0);
    QCOMPARE(mgr.passedChecks(), 0);
    QCOMPARE(mgr.failedChecks(), 0);
    QCOMPARE(mgr.pendingChecks(), mgr.totalChecks());
    // No vehicle connected -> no vehicle-specific critical check is mandatory yet.
    QVERIFY(mgr.allMandatoryPassed());
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
    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.mand"), QStringLiteral("Mand"), CheckCategory::Safety,
        QStringLiteral("Desc"), QStringList(), QStringList(), &mgr);
    check->setMandatory(true);
    mgr.addCheck(check);

    QVERIFY(!mgr.allMandatoryPassed());

    // Confirming a single check is not enough — every mandatory check must pass
    for (auto *c : mgr.checks()) {
        c->confirm();
    }
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

void PreflightManagerTest::testApplyVehicleKindBlocking()
{
    PreflightManager mgr;

    auto *motorCount = mgr.checkById(QStringLiteral("airframe.motor_count"));
    auto *motorSpin  = mgr.checkById(QStringLiteral("propulsion.motors.spin"));
    auto *airspeed   = mgr.checkById(QStringLiteral("sensors.airspeed"));
    auto *rtlAlt     = mgr.checkById(QStringLiteral("safety.rtl_alt"));

    QVERIFY(motorCount && motorSpin && airspeed && rtlAlt);

    // Defaults before any kind is applied
    QVERIFY(!motorCount->isBlocking());
    QVERIFY(!airspeed->isBlocking());
    QVERIFY(!rtlAlt->isBlocking());

    // Multirotor: motors block, airspeed/rtl_alt advisory — airspeed must
    // never block a quad from arming.
    mgr.applyVehicleKind(QStringLiteral("MULTIROTOR"));
    QVERIFY(motorCount->isBlocking());
    QVERIFY(motorSpin->isBlocking());
    QVERIFY(!airspeed->isBlocking());
    QVERIFY(!rtlAlt->isBlocking());

    // Fixed wing: airspeed + rtl_alt block; motor checks advisory.
    mgr.applyVehicleKind(QStringLiteral("FIXED_WING"));
    QVERIFY(!motorCount->isBlocking());
    QVERIFY(!motorSpin->isBlocking());
    QVERIFY(airspeed->isBlocking());
    QVERIFY(rtlAlt->isBlocking());

    // Unknown kind leaves the previous explicit decisions untouched.
    mgr.applyVehicleKind(QStringLiteral("UNKNOWN"));
    QVERIFY(airspeed->isBlocking());
}

UT_REGISTER_TEST(PreflightManagerTest)

#include "PreflightManagerTest.moc"
