#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "ChecklistItemModel.h"
#include "mocks/MockTelemetryBridge.h"

class ChecklistItemModelTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testEmptyModel();
    void testLoadFromJson();
    void testDataAccess();
    void testDefaultStatus();
    void testUpdateCheckSignal();
    void testUpdateCheckNoFalseSignals();
    void testCategoryFilter();
    void testCategoryFilterReset();
};

void ChecklistItemModelTest::testEmptyModel()
{
    ChecklistItemModel model;
    QCOMPARE(model.rowCount(), 0);
}

void ChecklistItemModelTest::testLoadFromJson()
{
    ChecklistItemModel model;
    QJsonArray items;
    for (int i = 0; i < 5; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk%1").arg(i);
        obj[QStringLiteral("label")] = QStringLiteral("Check %1").arg(i);
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }
    model.loadFromJson(items);
    QCOMPARE(model.rowCount(), 5);
}

void ChecklistItemModelTest::testDataAccess()
{
    ChecklistItemModel model;
    QJsonArray items;
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("chk1");
    obj[QStringLiteral("label")] = QStringLiteral("Battery Voltage");
    obj[QStringLiteral("isManual")] = false;
    items.append(obj);
    model.loadFromJson(items);

    QVariant idVal = model.data(model.index(0), ChecklistItemModel::IdRole);
    QCOMPARE(idVal.toString(), QStringLiteral("chk1"));

    QVariant labelVal = model.data(model.index(0), ChecklistItemModel::LabelRole);
    QCOMPARE(labelVal.toString(), QStringLiteral("Battery Voltage"));
}

void ChecklistItemModelTest::testDefaultStatus()
{
    ChecklistItemModel model;
    QJsonArray items;
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("chk1");
    items.append(obj);
    model.loadFromJson(items);

    QVariant statusVal = model.data(model.index(0), ChecklistItemModel::StatusRole);
    QCOMPARE(statusVal.toInt(), 0);
}

void ChecklistItemModelTest::testUpdateCheckSignal()
{
    ChecklistItemModel model;
    QJsonArray items;
    for (int i = 0; i < 5; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk%1").arg(i);
        items.append(obj);
    }
    model.loadFromJson(items);

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    model.setItemStatus(2, 1, QStringLiteral("PASS"));

    QCOMPARE(dataSpy.count(), 1);
    QList<QVariant> args = dataSpy.at(0);
    QModelIndex topLeft = args.at(0).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 2);
}

void ChecklistItemModelTest::testUpdateCheckNoFalseSignals()
{
    ChecklistItemModel model;
    QJsonArray items;
    for (int i = 0; i < 5; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk%1").arg(i);
        items.append(obj);
    }
    model.loadFromJson(items);

    model.setItemStatus(3, 1, QStringLiteral("PASS"));

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    model.setItemStatus(2, 1, QStringLiteral("PASS"));

    QCOMPARE(dataSpy.count(), 1);
    QModelIndex topLeft = dataSpy.at(0).at(0).value<QModelIndex>();
    QVERIFY(topLeft.row() == 2);
    QVERIFY(topLeft.row() != 0);
    QVERIFY(topLeft.row() != 1);
    QVERIFY(topLeft.row() != 3);
    QVERIFY(topLeft.row() != 4);
}

void ChecklistItemModelTest::testCategoryFilter()
{
    ChecklistItemModel model;
    QJsonArray items;
    QJsonObject nav;
    nav[QStringLiteral("id")] = QStringLiteral("nav1");
    nav[QStringLiteral("label")] = QStringLiteral("Navigation");
    nav[QStringLiteral("category")] = 1;
    items.append(nav);
    QJsonObject safety;
    safety[QStringLiteral("id")] = QStringLiteral("saf1");
    safety[QStringLiteral("label")] = QStringLiteral("Safety");
    safety[QStringLiteral("category")] = 2;
    items.append(safety);
    QJsonObject nav2;
    nav2[QStringLiteral("id")] = QStringLiteral("nav2");
    nav2[QStringLiteral("label")] = QStringLiteral("Navigation 2");
    nav2[QStringLiteral("category")] = 1;
    items.append(nav2);
    model.loadFromJson(items);

    QCOMPARE(model.rowCount(), 3);
}

void ChecklistItemModelTest::testCategoryFilterReset()
{
    ChecklistItemModel model;
    QJsonArray items;
    for (int i = 0; i < 5; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk%1").arg(i);
        obj[QStringLiteral("category")] = (i < 2) ? 1 : 2;
        items.append(obj);
    }
    model.loadFromJson(items);
    QCOMPARE(model.rowCount(), 5);

    model.loadFromJson({});
    QCOMPARE(model.rowCount(), 0);

    model.loadFromJson(items);
    QCOMPARE(model.rowCount(), 5);
}

UT_REGISTER_TEST(ChecklistItemModelTest)

#include "ChecklistItemModelTest.moc"
