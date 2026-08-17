#include "CheckSmokeTest.h"

#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

#include "AbstractCheck.h"
#include "ManualConfirmCheck.h"
#include "ChecklistItemModel.h"
#include "ChecklistEngine.h"

// Concrete test check that extends AbstractCheck with simple pass/fail logic
class TestPassCheck : public AbstractCheck {
    Q_OBJECT
public:
    TestPassCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("test.pass"), QStringLiteral("Test Pass"),
                        CheckCategory::Safety, CheckType::Auto, true, false, parent)
    {}
    void evaluate() override { setStatus(CheckStatus::Passed, QStringLiteral("OK")); }
    // Expose protected setStatus for testing
    void forceStatus(CheckStatus s, const QString &msg = {}) { setStatus(s, msg); }
};

class TestFailCheck : public AbstractCheck {
    Q_OBJECT
public:
    TestFailCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("test.fail"), QStringLiteral("Test Fail"),
                        CheckCategory::Safety, CheckType::Auto, true, false, parent)
    {}
    void evaluate() override { setStatus(CheckStatus::Failed, QStringLiteral("FAIL")); }
};

class TestManualCheck : public AbstractCheck {
    Q_OBJECT
public:
    TestManualCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("test.manual"), QStringLiteral("Test Manual"),
                        CheckCategory::Safety, CheckType::Manual, true, true, parent)
    {}
    void evaluate() override { setStatus(CheckStatus::Pending, QStringLiteral("Awaiting confirm")); }
};

// ── AbstractCheck lifecycle ────────────────────────────────────────────────

void CheckSmokeTest::testCheckInitialState()
{
    TestPassCheck check;
    QCOMPARE(check.id(), QStringLiteral("test.pass"));
    QCOMPARE(check.label(), QStringLiteral("Test Pass"));
    QCOMPARE(check.status(), CheckStatus::Pending);
    QCOMPARE(check.statusInt(), 0);
    QCOMPARE(check.category(), CheckCategory::Safety);
    QCOMPARE(check.checkType(), CheckType::Auto);
    QVERIFY(check.mandatory());
    QVERIFY(!check.canOverride());
    QVERIFY(check.isAuto());
    QCOMPARE(check.message(), QString());
    QVERIFY(!check.currentValue().isValid());
}

void CheckSmokeTest::testCheckStatusTransitions()
{
    TestPassCheck check;
    QCOMPARE(check.status(), CheckStatus::Pending);

    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Passed);
    QCOMPARE(check.message(), QStringLiteral("OK"));

    check.forceStatus(CheckStatus::Pending, QStringLiteral("reset"));
    QCOMPARE(check.status(), CheckStatus::Pending);
    QCOMPARE(check.message(), QStringLiteral("reset"));

    check.reset();
    QCOMPARE(check.status(), CheckStatus::Pending);
}

void CheckSmokeTest::testCheckOverrideMechanism()
{
    TestFailCheck check;
    check.evaluate();
    QCOMPARE(check.status(), CheckStatus::Failed);
}

void CheckSmokeTest::testCheckCannotOverrideAuto()
{
    TestPassCheck autoCheck;
    QVERIFY(!autoCheck.canOverride());
}

// ── ManualConfirmCheck ─────────────────────────────────────────────────────

void CheckSmokeTest::testManualConfirmDefault()
{
    ManualConfirmCheck check(
        QStringLiteral("test.manual.confirm"),
        QStringLiteral("Confirm Test"),
        CheckCategory::Airframe,
        QStringLiteral("Please confirm"),
        QStringList(),
        QStringList(),
        this);

    QCOMPARE(check.id(), QStringLiteral("test.manual.confirm"));
    QCOMPARE(check.status(), CheckStatus::Pending);
    QCOMPARE(check.checkType(), CheckType::Manual);
    QVERIFY(!check.isAuto());
}

void CheckSmokeTest::testManualInspectionItems()
{
    ManualConfirmCheck check(
        QStringLiteral("test.manual.inspect"),
        QStringLiteral("Visual Inspection"),
        CheckCategory::Airframe,
        QStringLiteral("Perform visual inspection"),
        {QStringLiteral("Damage inspection"), QStringLiteral("Screws")},
        {},
        this);

    QCOMPARE(check.inspectionItems().size(), 2);
    QCOMPARE(check.inspectionItems().at(0), QStringLiteral("Damage inspection"));
    QVERIFY(check.inspectionItems().contains(QStringLiteral("Screws")));
}

void CheckSmokeTest::testManualConfirmFlow()
{
    ManualConfirmCheck check(
        QStringLiteral("test.manual.confirm2"),
        QStringLiteral("Confirm Test 2"),
        CheckCategory::Airframe,
        QStringLiteral("Please confirm manually"),
        QStringList(),
        QStringList(),
        this);

    QCOMPARE(check.status(), CheckStatus::Pending);

    bool ok = check.confirm(QStringLiteral("Operator confirms"));
    QVERIFY(ok);
    QCOMPARE(check.status(), CheckStatus::Passed);

    check.reset();
    QCOMPARE(check.status(), CheckStatus::Pending);
}

// ── ChecklistItemModel ─────────────────────────────────────────────────────

void CheckSmokeTest::testModelLoadFromJson()
{
    ChecklistItemModel model;
    QCOMPARE(model.rowCount(), 0);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk1");
        obj[QStringLiteral("label")] = QStringLiteral("Battery Voltage");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("unit")] = QStringLiteral("V");
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk2");
        obj[QStringLiteral("label")] = QStringLiteral("GPS Fix");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("gpsFixType");
        obj[QStringLiteral("requiredValue")] = 3.0;
        obj[QStringLiteral("tolerance")] = 0;
        obj[QStringLiteral("unit")] = QString();
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }

    model.loadFromJson(items);
    QCOMPARE(model.rowCount(), 2);

    QModelIndex idx0 = model.index(0);
    QCOMPARE(model.data(idx0, ChecklistItemModel::IdRole).toString(), QStringLiteral("chk1"));
    QCOMPARE(model.data(idx0, ChecklistItemModel::LabelRole).toString(), QStringLiteral("Battery Voltage"));
    QCOMPARE(model.data(idx0, ChecklistItemModel::BindPropertyRole).toString(), QStringLiteral("batteryVoltage"));
    QCOMPARE(model.data(idx0, ChecklistItemModel::RequiredValueRole).toDouble(), 12.6);
    QCOMPARE(model.data(idx0, ChecklistItemModel::ToleranceRole).toDouble(), 0.5);
    QCOMPARE(model.data(idx0, ChecklistItemModel::UnitRole).toString(), QStringLiteral("V"));
    QCOMPARE(model.data(idx0, ChecklistItemModel::IsManualRole).toBool(), false);
    QCOMPARE(model.data(idx0, ChecklistItemModel::StatusRole).toInt(), 0);
}

void CheckSmokeTest::testModelSetItemStatus()
{
    ChecklistItemModel model;
    QJsonArray items;
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("chk1");
    obj[QStringLiteral("label")] = QStringLiteral("Battery");
    items.append(obj);
    model.loadFromJson(items);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::StatusRole).toInt(), 0);

    QSignalSpy statusSpy(&model, &ChecklistItemModel::itemStatusChanged);
    model.setItemStatus(0, 1, QStringLiteral("Passed"));
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::StatusRole).toInt(), 1);
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::MessageRole).toString(), QStringLiteral("Passed"));
    QCOMPARE(statusSpy.count(), 1);

    model.setItemStatus(0, 2, QStringLiteral("Failed"));
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::StatusRole).toInt(), 2);
    QCOMPARE(statusSpy.count(), 2);
}

void CheckSmokeTest::testModelReset()
{
    ChecklistItemModel model;
    QJsonArray items;
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("chk1");
    items.append(obj);
    model.loadFromJson(items);
    model.setItemStatus(0, 1, QStringLiteral("Passed"));
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::StatusRole).toInt(), 1);

    model.resetAll();
    QCOMPARE(model.data(model.index(0), ChecklistItemModel::StatusRole).toInt(), 0);
}

// ── ChecklistEngine basic tests ────────────────────────────────────────────

void CheckSmokeTest::testEngineAllPassedDetection()
{
    ChecklistEngine engine;

    QVERIFY(!engine.allPassed());

    ChecklistItemModel model;
    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk1");
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }
    model.loadFromJson(items);

    engine.setModel(&model);

    QVERIFY(!engine.allPassed());
    QCOMPARE(engine.totalItems(), 1);
    QCOMPARE(engine.pendingItems(), 1);
    QCOMPARE(engine.passedItems(), 0);
}

UT_REGISTER_TEST(CheckSmokeTest)

#include "CheckSmokeTest.moc"
