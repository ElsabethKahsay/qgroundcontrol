#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "PreflightManager.h"
#include "AbstractCheck.h"
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
};

void PreflightManagerTest::testInitialState()
{
    PreflightManager mgr;
    QCOMPARE(mgr.totalChecks(), 0);
    QCOMPARE(mgr.passedChecks(), 0);
    QCOMPARE(mgr.failedChecks(), 0);
    QCOMPARE(mgr.pendingChecks(), 0);
    QVERIFY(mgr.allMandatoryPassed());
    QCOMPARE(mgr.completionPercent(), 0);
}

void PreflightManagerTest::testAddCheck()
{
    PreflightManager mgr;

    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.manual.confirm"),
        QStringLiteral("Test Check"), CheckCategory::Safety,
        QStringLiteral("Test description"), {}, &mgr);

    mgr.addCheck(check);
    QCOMPARE(mgr.totalChecks(), 1);
    QVERIFY(mgr.checkById(QStringLiteral("test.manual.confirm")) != nullptr);
}

void PreflightManagerTest::testCheckById()
{
    PreflightManager mgr;
    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.findable"),
        QStringLiteral("Findable"), CheckCategory::Safety,
        QStringLiteral("Desc"), {}, &mgr);
    mgr.addCheck(check);

    QVERIFY(mgr.checkById(QStringLiteral("test.findable")) != nullptr);
    QVERIFY(mgr.checkById(QStringLiteral("nonexistent")) == nullptr);
}

void PreflightManagerTest::testProgressCounters()
{
    PreflightManager mgr;

    auto *check1 = new ManualConfirmCheck(
        QStringLiteral("test.a"), QStringLiteral("A"), CheckCategory::Safety,
        QStringLiteral("Desc"), {}, &mgr);
    auto *check2 = new ManualConfirmCheck(
        QStringLiteral("test.b"), QStringLiteral("B"), CheckCategory::Safety,
        QStringLiteral("Desc"), {}, &mgr);
    mgr.addCheck(check1);
    mgr.addCheck(check2);

    QCOMPARE(mgr.totalChecks(), 2);
    QCOMPARE(mgr.pendingChecks(), 2);

    check1->confirm();
    QCOMPARE(mgr.passedChecks(), 1);
    QCOMPARE(mgr.pendingChecks(), 1);
}

void PreflightManagerTest::testAllMandatoryPassed()
{
    PreflightManager mgr;
    auto *check = new ManualConfirmCheck(
        QStringLiteral("test.mand"), QStringLiteral("Mand"), CheckCategory::Safety,
        QStringLiteral("Desc"), {}, &mgr);
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
        QStringLiteral("Desc"), {}, &mgr);
    mgr.addCheck(check);

    check->confirm();
    QCOMPARE(check->status(), CheckStatus::Passed);

    QSignalSpy progressSpy(&mgr, &PreflightManager::progressChanged);
    mgr.resetAll();

    QCOMPARE(check->status(), CheckStatus::Pending);
    QVERIFY(progressSpy.count() > 0);
}

UT_REGISTER_TEST(PreflightManagerTest)

#include "PreflightManagerTest.moc"
