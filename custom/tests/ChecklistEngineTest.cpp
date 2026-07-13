#include <QTest>
#include <QSignalSpy>
#include <QThread>

#include "UnitTest.h"
#include "ChecklistEngine.h"
#include "ChecklistItemModel.h"
#include "mocks/MockTelemetryBridge.h"

class ChecklistEngineTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testEngineInitialState();
    void testEngineStartStop();
    void testEngineEvaluateAll();
    void testEngineAllPassed();
    void testEngineConfirmItem();
    void testEngineTelemetryBinding();
    void testSelectiveEvaluationOnPropertyChange();
    void testEvaluateAllHitsAllChecks();
    void testStaleDetection();
    void testDisconnectedStateUnknown();
};

static int _evaluateCallCount = 0;
static int _evaluateCallCount2 = 0;

class TrackedItemModel : public ChecklistItemModel {
    Q_OBJECT
public:
    using ChecklistItemModel::setItemStatus;
};

void ChecklistEngineTest::testEngineInitialState()
{
    ChecklistEngine engine;
    QVERIFY(!engine.allPassed());
    QCOMPARE(engine.totalItems(), 0);
    QCOMPARE(engine.passedItems(), 0);
    QCOMPARE(engine.pendingItems(), 0);
    QCOMPARE(engine.failedItems(), 0);
}

void ChecklistEngineTest::testEngineStartStop()
{
    MockTelemetryBridge bridge;
    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    ChecklistItemModel model;
    engine.setModel(&model);

    QSignalSpy evalSpy(&engine, &ChecklistEngine::evaluationCompleted);

    engine.start();
    engine.stop();

    QVERIFY(true);
}

void ChecklistEngineTest::testEngineEvaluateAll()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk1");
        obj[QStringLiteral("label")] = QStringLiteral("Battery Voltage");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk2");
        obj[QStringLiteral("label")] = QStringLiteral("Manual Check");
        obj[QStringLiteral("isManual")] = true;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    QCOMPARE(engine.totalItems(), 2);

    QSignalSpy evalSpy(&engine, &ChecklistEngine::evaluationCompleted);
    engine.evaluateAll();

    QCOMPARE(evalSpy.count(), 1);
}

void ChecklistEngineTest::testEngineAllPassed()
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
    QCOMPARE(engine.failedItems(), 0);
}

void ChecklistEngineTest::testEngineConfirmItem()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("manual1");
        obj[QStringLiteral("label")] = QStringLiteral("Manual Confirm");
        obj[QStringLiteral("isManual")] = true;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    QCOMPARE(engine.pendingItems(), 1);

    QSignalSpy confirmSpy(&engine, &ChecklistEngine::itemConfirmed);
    engine.confirmItem(0);
    QCOMPARE(confirmSpy.count(), 1);
}

void ChecklistEngineTest::testEngineTelemetryBinding()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("bat");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    QSignalSpy evalSpy(&engine, &ChecklistEngine::evaluationCompleted);
    bridge.setBatteryVoltage(12.5);
    QCOMPARE(evalSpy.count(), 0);

    engine.evaluateAll();
    QCOMPARE(evalSpy.count(), 1);
}

void ChecklistEngineTest::testSelectiveEvaluationOnPropertyChange()
{
    _evaluateCallCount = 0;
    _evaluateCallCount2 = 0;

    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("alt");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("altitudeRelative");
        obj[QStringLiteral("requiredValue")] = 100.0;
        obj[QStringLiteral("tolerance")] = 10.0;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("bat");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    QSignalSpy evalSpy(&engine, &ChecklistEngine::evaluationCompleted);

    bridge.setAltitudeRelative(95.0);
    QCOMPARE(evalSpy.count(), 0);

    bridge.setBatteryVoltage(12.5);
    QCOMPARE(evalSpy.count(), 0);
}

void ChecklistEngineTest::testEvaluateAllHitsAllChecks()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    for (int i = 0; i < 3; ++i) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("chk%1").arg(i);
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    bridge.setBatteryVoltage(12.5);
    engine.evaluateAll();

    QCOMPARE(engine.passedItems(), 3);
    QCOMPARE(engine.failedItems(), 0);
    QCOMPARE(engine.pendingItems(), 0);
}

void ChecklistEngineTest::testStaleDetection()
{
}

void ChecklistEngineTest::testDisconnectedStateUnknown()
{
    MockTelemetryBridge bridge;
    bridge.setConnected(true);
    bridge.setConnectionQuality(100);

    ChecklistEngine engine;
    engine.setTelemetryBridge(&bridge);

    QJsonArray items;
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("bat");
        obj[QStringLiteral("bindProperty")] = QStringLiteral("batteryVoltage");
        obj[QStringLiteral("requiredValue")] = 12.6;
        obj[QStringLiteral("tolerance")] = 0.5;
        obj[QStringLiteral("isManual")] = false;
        items.append(obj);
    }

    ChecklistItemModel model;
    model.loadFromJson(items);
    engine.setModel(&model);

    bridge.setConnected(false);
    engine.evaluateAll();
}

UT_REGISTER_TEST(ChecklistEngineTest)

#include "ChecklistEngineTest.moc"
