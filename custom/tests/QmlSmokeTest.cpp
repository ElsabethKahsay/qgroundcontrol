/**
 * @file QmlSmokeTest.cpp
 * @brief P2-2 — QML smoke tests for MotorCheckPanel and MainStatusIndicator.
 *
 * Instantiates the real QML components (qrc resources) inside the same engine
 * the app builds (PreflightPlugin::createQmlApplicationEngine), so every
 * com.uav.preflight singleton and context property is live. The tests then
 * drive real state transitions (vehicle connect, arm/disarm, gate block) and
 * assert the bindings that historically regressed silently in QML.
 *
 * The unit-test process never reaches the normal app boot path, so
 * PreflightPlugin::init() (which creates all the managers the plugin exposes as
 * QML context properties) normally never runs here — the smoke suites call it
 * themselves before creating the engine, then override the few context
 * properties whose instances need to be test-controlled (e.g. the gate).
 *
 * P2-3 note: the gate-blocked click path asserts that a blocked arm is
 * *not* transmitted to the vehicle — all arm widgets route through the single
 * TelemetryProvider/ArmingGate decision (P0 arm consolidation).
 */

#include <QTest>
#include <QSignalSpy>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QJSValue>
#include <QJSEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include "UnitTest.h"
#include "QGCCorePlugin.h"
#include "PreflightPlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "PreflightManager.h"
#include "ArmingGate.h"
#include "AbstractCheck.h"
#include "HardwareTestController.h"
#include "mocks/MockTelemetryBridge.h"
#include "QGCPalette.h"

// ---------------------------------------------------------------------------
// Harness: unit-test mode skips the normal app boot, so PreflightPlugin::init()
// (which constructs every manager the plugin exposes to QML) never runs. Call
// it once per test process so createQmlApplicationEngine exposes real context
// properties instead of silently skipping its null-guarded registrations.
// ---------------------------------------------------------------------------

static void preflightInitOnce()
{
    static bool initialized = false;
    if (!initialized) {
        PreflightPlugin::instance()->init();
        initialized = true;
    }
}

// ---------------------------------------------------------------------------
// Tiny helpers shared by both smoke suites.
// ---------------------------------------------------------------------------

static void pumpQml(int ms)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QTest::qWait(ms);
}

static bool pumpUntilQml(bool (*cond)(void *ctx), void *ctx, int timeoutMs)
{
    const quint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (cond(ctx)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(50);
    }
    return cond(ctx);
}

// Recursively find the first text within `item` matching `needle`.
static QString firstTextContaining(QQuickItem *item, const QString &needle, int depth = 0)
{
    if (!item || depth > 24) return QString();
    const QString text = item->property("text").toString();
    if (text.contains(needle)) return text;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        const QString hit = firstTextContaining(child, needle, depth + 1);
        if (!hit.isEmpty()) return hit;
    }
    return QString();
}

// Recursively find an item whose `text` property equals `value`.
static QQuickItem *findItemByText(QQuickItem *item, const QString &text, int depth = 0)
{
    if (!item || depth > 24) return nullptr;
    if (item->property("text").toString() == text) return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *hit = findItemByText(child, text, depth + 1)) return hit;
    }
    return nullptr;
}

// Call a JS function declared on a QML item. QMetaObject::invokeMethod cannot
// reliably invoke QML functions that return values or take parameters, so we
// call them the way QML itself does — through the engine's JS value.
static QVariant callQmlFunction(QQuickItem *item, const QString &name, const QVariantList &args = {})
{
    QQmlEngine *engine = qmlEngine(item);
    if (!engine) return QVariant();
    QJSValue jsItem = engine->newQObject(item);
    const QJSValue fn = jsItem.property(name);
    if (!fn.isCallable()) return QVariant();
    QJSValueList jsArgs;
    for (const QVariant &arg : args) jsArgs.append(engine->toScriptValue(arg));
    return fn.call(jsArgs).toVariant();
}

static QQuickWindow *createWindow(const QSharedPointer<QQmlApplicationEngine> &engine, int w, int h)
{
    QQuickWindow *window = new QQuickWindow;
    window->resize(w, h);
    engine->rootContext()->setContextProperty(QStringLiteral("mainWindow"), window);
    window->show();
    return window;
}

// Instantiate `qmlFile` inside `engine` and reparent to `window`.
static QQuickItem *createComponent(const QSharedPointer<QQmlApplicationEngine> &engine,
                                   QQuickWindow *window, const char *qmlFile, QString *err = nullptr)
{
    QQmlComponent component(engine.data(), QUrl(QString::fromLatin1(qmlFile)));
    if (component.status() != QQmlComponent::Ready) {
        if (err) *err = component.errorString();
        return nullptr;
    }
    QObject *created = component.create();
    QQuickItem *item = qobject_cast<QQuickItem *>(created);
    if (item) item->setParentItem(window->contentItem());
    return item;
}

// ---------------------------------------------------------------------------
// Forced-failed mandatory check used to drive the live gate into a blocked
// state deterministically (mirrors TestFlowCheck in ArmingFlowIntegrationTest).
// Must live at file scope (not anonymous namespace) so moc can generate
// meta-object code for Q_OBJECT.
// ---------------------------------------------------------------------------

class BlockingCheck : public AbstractCheck {
    Q_OBJECT
public:
    explicit BlockingCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("qmlsmoke.block"), QStringLiteral("Blocking"),
                        CheckCategory::Safety, CheckType::Auto, true, true, parent) {}
    void evaluate() override {}
    void block() { setStatus(CheckStatus::Failed, QStringLiteral("Smoke-test block")); }
};

// ---------------------------------------------------------------------------
// Connect a real ArduPlane mock (same pattern the control-surface tests use).
// ---------------------------------------------------------------------------

static bool connectPlane(Vehicle **outVehicle, MockLink **outMockLink, int timeoutMs = 30000)
{
    MockLink *mockLink = MockLink::startAPMArduPlaneMockLink(false);
    *outMockLink = mockLink;
    if (!mockLink) return false;

    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    if (!spyVehicle.wait(10000)) return false;
    Vehicle *vehicle = MultiVehicleManager::instance()->activeVehicle();
    *outVehicle = vehicle;
    if (!vehicle) return false;

    QSignalSpy spyPlan(vehicle, &Vehicle::initialConnectComplete);
    if (!spyPlan.wait(timeoutMs)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// MotorCheckPanelSmokeTest
// ---------------------------------------------------------------------------

class MotorCheckPanelSmokeTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }
    void cleanupTestCase();

    void testLoadsWithoutVehicle();
    void testDialogEntrySafeWithoutVehicle();
    void testTracksBoundFixedWingVehicle();

private:
    QSharedPointer<QQmlApplicationEngine> _engine;
    QSharedPointer<QQuickWindow>          _window;
    QSharedPointer<QQuickItem>            _root;
    HardwareTestController                _htc;
    Vehicle *_vehicle = nullptr;
    MockLink *_mockLink = nullptr;

    void _loadComponent();
};

void MotorCheckPanelSmokeTest::cleanupTestCase()
{
    if (_mockLink) {
        _mockLink->disconnect();
        pumpQml(100);
    }
    if (_root) _root->setParentItem(nullptr);
    _root.clear();
    _window.clear();
    _engine.clear();
}

void MotorCheckPanelSmokeTest::_loadComponent()
{
    preflightInitOnce();
    _engine.reset(QGCCorePlugin::instance()->createQmlApplicationEngine(nullptr));
    QVERIFY2(_engine, "createQmlApplicationEngine returned null");
    QVERIFY2(_engine->rootContext()->contextProperty(QStringLiteral("HardwareTestController")).isValid(),
             "HardwareTestController context property missing — PreflightPlugin::init() skipped?");

    // Bind the panel to our own controller so vehicle-bound tests observe a
    // controller that is actually connected to the mock plane.
    _engine->rootContext()->setContextProperty(QStringLiteral("HardwareTestController"), &_htc);

    _window.reset(createWindow(_engine, 900, 600));
    QVERIFY(_window);

    QString err;
    _root.reset(createComponent(_engine, _window.data(), "qrc:/qml/cpts/MotorCheckPanel.qml", &err));
    QVERIFY2(_root, qPrintable(err));
}

void MotorCheckPanelSmokeTest::testLoadsWithoutVehicle()
{
    _loadComponent();

    // With no vehicle the panel must show the neutral badge and the
    // "waiting for parameters" placeholder — bindings must resolve, not throw.
    QCOMPARE(QQmlProperty(_root.data(), "_mc").read().toInt(), 0);
    QVERIFY(!QQmlProperty(_root.data(), "_armed").read().toBool());

    QVERIFY(!firstTextContaining(_root.data(), QStringLiteral("Motor Test")).isEmpty());
    QVERIFY(!firstTextContaining(_root.data(), QStringLiteral("Waiting for vehicle parameters")).isEmpty());
    QVERIFY(!firstTextContaining(_root.data(), QStringLiteral("Open Motor Test")).isEmpty());

    // testLabel() (the dialog/card label logic) must be callable.
    QCOMPARE(callQmlFunction(_root.data(), QStringLiteral("testLabel"), { 1 }).toString(), QStringLiteral("M1"));
}

void MotorCheckPanelSmokeTest::testDialogEntrySafeWithoutVehicle()
{
    _loadComponent();

    // openDialog() opens the motor-test dialog; without a vehicle it must be a
    // safe no-op that leaves the root tree intact.
    QVERIFY(QMetaObject::invokeMethod(_root.data(), "openDialog"));
    QVERIFY(_root);
    pumpQml(50);
    QVERIFY(_root);
}

void MotorCheckPanelSmokeTest::testTracksBoundFixedWingVehicle()
{
    QVERIFY2(connectPlane(&_vehicle, &_mockLink), "failed to connect ArduPlane mock");
    _htc.setVehicle(_vehicle);

    _loadComponent();

    // The QML counter binding must track the mock plane's resolved motor count.
    QVERIFY2(pumpUntilQml([](void *ctx) {
        return QQmlProperty(static_cast<MotorCheckPanelSmokeTest *>(ctx)->_root.data(), "_mc")
                       .read().toInt() >= 1;
    }, this, 20000), "motor count never resolved for fixed-wing mock");

    QVERIFY(!QQmlProperty(_root.data(), "_vt").read().toString().isEmpty());
    QVERIFY(!QQmlProperty(_root.data(), "_armed").read().toBool());

    // The launcher row must now show the resolved vehicle/motor line and the
    // DISARM badge must stay HIDDEN while the vehicle is disarmed.
    QVERIFY(!firstTextContaining(_root.data(), QStringLiteral("detected")).isEmpty());
    QQuickItem *disarmBadge = findItemByText(_root.data(), QStringLiteral("\u26A0 DISARM"));
    QVERIFY(disarmBadge);
    QVERIFY(!disarmBadge->property("visible").toBool());

    // With a fixed-wing vehicle the per-motor label takes the fixed-wing path.
    const QString label = callQmlFunction(_root.data(), QStringLiteral("testLabel"), { 1 }).toString();
    QVERIFY2(label != QStringLiteral("M1") && !label.isEmpty(),
             qPrintable(QStringLiteral("fixed-wing label unresolved: '%1'").arg(label)));
}

// ---------------------------------------------------------------------------
// MainStatusIndicatorSmokeTest
// ---------------------------------------------------------------------------

class MainStatusIndicatorSmokeTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override;
    void cleanupTestCase();

    void testDisconnectedState();
    void testConnectedState();
    void testArmedStateRendered();
    void testBlockedArmRouterDoesNotTransmit();

private:
    QSharedPointer<QQmlApplicationEngine> _engine;
    QSharedPointer<QQuickWindow>          _window;
    QSharedPointer<QQuickItem>            _root;
    Vehicle *_vehicle = nullptr;
    MockLink *_mockLink = nullptr;

    // Heavy objects are heap-allocated lazily inside _loadComponent()/tests:
    // this test object is constructed during static init (UT_REGISTER_TEST),
    // i.e. BEFORE QCoreApplication exists — a member with production-weight
    // side effects (PreflightManager ctor, TelemetryBridge) would abort there.
    MockTelemetryBridge *_bridge = nullptr;
    PreflightManager    *_preflightManager = nullptr;
    ArmingGate          *_gate = nullptr;
    BlockingCheck       *_blockingCheck = nullptr;
    QGCPalette          *_palette = nullptr;

    void _loadComponent();
    QString _statusLabel() const;
    void _connectPlane();
    void _ensureDisarmed();
    void _armVehicle();
};

void MainStatusIndicatorSmokeTest::cleanup()
{
    UnitTest::cleanup();
}

void MainStatusIndicatorSmokeTest::cleanupTestCase()
{
    if (_mockLink) {
        _mockLink->disconnect();
        pumpQml(100);
    }
    if (_root) _root->setParentItem(nullptr);
    _root.clear();
    _window.clear();
    _engine.clear();

    delete _blockingCheck; _blockingCheck = nullptr;
    delete _gate;          _gate = nullptr;
    delete _preflightManager; _preflightManager = nullptr;
    delete _bridge;        _bridge = nullptr;
    delete _palette;       _palette = nullptr;
}

void MainStatusIndicatorSmokeTest::_loadComponent()
{
    preflightInitOnce();

    // The real app keeps ONE QML engine for its whole lifetime. The suite also
    // shares one engine+window across every test (destroying a fresh engine per
    // test hit a Qt6 AOT use-after-free during teardown).
    if (!_engine) {
        _engine.reset(QGCCorePlugin::instance()->createQmlApplicationEngine(nullptr));
        QVERIFY2(_engine, "createQmlApplicationEngine returned null");

        // The real app exposes the active palette to every QML view through the
        // `qgcPal` root-context property; supply it here too or QGroundControl
        // controls throw ReferenceErrors.
        if (!_palette) _palette = new QGCPalette(this);
        _engine->rootContext()->setContextProperty(QStringLiteral("qgcPal"), _palette);
    }
    if (!_window) {
        _window.reset(createWindow(_engine, 1100, 700));
        QVERIFY(_window);
    }

    // Drop the previous test's root component before mounting the next. Reap
    // its QML scopes (the engine's stock AOT components log during teardown and
    // touching freed string data crashed the process) before creating a new one.
    if (_root) _root->setParentItem(nullptr);
    _root.clear();
    if (_engine) {
        _engine->collectGarbage();
        pumpQml(25);
    }

    QString err;
    // The custom file shares its qrc alias with the stock MainStatusIndicator
    // (which the compiled QML module also caches), so the URL `qrc:...` always
    // resolves to the STOCK component here. Load the real custom file from disk
    // to exercise the component this product actually ships.
    QDir srcDir(QCoreApplication::applicationDirPath());   // <build>/Debug
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(srcDir.filePath(QStringLiteral("custom/qml/MainStatusIndicator.qml")))) {
            break;
        }
        if (!srcDir.cdUp()) {
            break;
        }
    }
    const QString customFile = srcDir.filePath(QStringLiteral("custom/qml/MainStatusIndicator.qml"));
    QVERIFY2(QFileInfo::exists(customFile), qPrintable(QStringLiteral("missing %1").arg(customFile)));
    _root.reset(createComponent(_engine, _window.data(), customFile.toUtf8().constData(), &err));
    QVERIFY2(_root, qPrintable(err));
}

// The status text lives on the mainStatusLabel QGCLabel; resolve it through
// a text scan of the rendered tree.
QString MainStatusIndicatorSmokeTest::_statusLabel() const
{
    for (const QString &candidate : { QStringLiteral("Disconnected"), QStringLiteral("Armed"),
                                      QStringLiteral("Flying"), QStringLiteral("Landing"),
                                      QStringLiteral("Ready To Fly"), QStringLiteral("Not Ready"),
                                      QStringLiteral("Communication Lost") }) {
        if (!firstTextContaining(_root.data(), candidate).isEmpty()) return candidate;
    }
    return QString();
}

// The class reuses ONE mock plane across tests: connecting a second mock in
// the same process doesn't change MultiVehicleManager::activeVehicle, so a
// second spy-wait would deadlock silently.
void MainStatusIndicatorSmokeTest::_connectPlane()
{
    if (_vehicle && _mockLink) return;
    QVERIFY2(connectPlane(&_vehicle, &_mockLink), "failed to connect ArduPlane mock");
}

void MainStatusIndicatorSmokeTest::_ensureDisarmed()
{
    if (!_vehicle || !_vehicle->armed()) return;
    _vehicle->sendMavCommand(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_COMPONENT_ARM_DISARM, false, 0.0f);
    QVERIFY2(pumpUntilQml([](void *ctx) {
        return !static_cast<MainStatusIndicatorSmokeTest *>(ctx)->_vehicle->armed();
    }, this, 15000), "vehicle never reached disarmed state");
}

void MainStatusIndicatorSmokeTest::_armVehicle()
{
    _vehicle->sendMavCommand(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_COMPONENT_ARM_DISARM, false, 1.0f);
    QVERIFY2(pumpUntilQml([](void *ctx) {
        return static_cast<MainStatusIndicatorSmokeTest *>(ctx)->_vehicle->armed();
    }, this, 15000), "vehicle never reached armed state");
}

void MainStatusIndicatorSmokeTest::testDisconnectedState()
{
    _loadComponent();

    // No vehicle: the indicator must read Disconnected and never crash.
    QCOMPARE(_statusLabel(), QStringLiteral("Disconnected"));
    QVERIFY(!QQmlProperty(_root.data(), "_armed").read().toBool());
    QCOMPARE(QQmlProperty(_root.data(), "_estRangeText").read().toString(), QString());
}

void MainStatusIndicatorSmokeTest::testConnectedState()
{
    _connectPlane();
    _loadComponent();

    // Connected + disarmed: label must leave Disconnected for a pre-arm state
    // (Ready To Fly / Not Ready depending on the mock's check report).
    QString label = _statusLabel();
    QVERIFY2(label != QStringLiteral("Disconnected") && !label.isEmpty(),
             qPrintable(QStringLiteral("label stuck at '%1'").arg(label)));
}

void MainStatusIndicatorSmokeTest::testArmedStateRendered()
{
    _connectPlane();
    _loadComponent();
    _armVehicle();

    // Armed branch must ALWAYS render "Armed" (P0: armed overrides the
    // pre-arm check report).
    QVERIFY2(pumpUntilQml([](void *ctx) {
        return static_cast<MainStatusIndicatorSmokeTest *>(ctx)->_statusLabel() == QStringLiteral("Armed");
    }, this, 15000), "label never reached Armed after arming");
    QVERIFY(QQmlProperty(_root.data(), "_armed").read().toBool());
}

void MainStatusIndicatorSmokeTest::testBlockedArmRouterDoesNotTransmit()
{
    _connectPlane();
    _ensureDisarmed();

    // Use a completely fresh engine for this test to avoid AOT state pollution
    // from the shared engine that crashes during teardown with broken-scope
    // popup components (Drawer, FlightModeMenuIndicator, ParameterEditorDialog).
    // Use FUNCTION-LOCAL STATIC QSharedPointers so the engine/window/root
    // persist for the process lifetime and are NEVER destroyed during test
    // object teardown (which triggers AOT console.log crash with freed strings
    // in stock components: Drawer.control undefined, FlightModeMenuIndicator.modelData
    // undefined). The OS reclaims memory on process exit.
    static QSharedPointer<QQmlApplicationEngine> gateEngine;
    static QSharedPointer<QQuickWindow> gateWindow;
    static QSharedPointer<QQuickItem> gateRoot;

    if (!gateEngine) {
        gateEngine.reset(QGCCorePlugin::instance()->createQmlApplicationEngine(nullptr));
        QVERIFY2(gateEngine, "createQmlApplicationEngine returned null for gate test");
        if (!_palette) _palette = new QGCPalette(this);
        gateEngine->rootContext()->setContextProperty(QStringLiteral("qgcPal"), _palette);
    }
    if (!gateWindow) {
        gateWindow.reset(createWindow(gateEngine, 1100, 700));
        QVERIFY(gateWindow);
    }

    // Install the deterministic local gate + forced-failed mandatory check.
    MockTelemetryBridge *gateBridge = new MockTelemetryBridge(this);
    PreflightManager *gatePreflightManager = new PreflightManager(1, this);
    ArmingGate *gate = new ArmingGate(this);
    BlockingCheck *gateBlockingCheck = new BlockingCheck(this);

    gateBridge->setConnected(true);
    gateBridge->setConnectionQuality(100);
    gatePreflightManager->setTelemetryBridge(gateBridge);
    gate->setMode(ArmingGate::Active);
    gate->setPreflightManager(gatePreflightManager);
    gate->setTelemetryBridge(gateBridge);
    gateBridge->setArmingGate(gate);

    gateBlockingCheck->block();
    gatePreflightManager->addCheck(gateBlockingCheck);

    gateEngine->rootContext()->setContextProperty(QStringLiteral("TelemetryProvider"), gateBridge);
    gateEngine->rootContext()->setContextProperty(QStringLiteral("PreflightManager"), gatePreflightManager);
    gateEngine->rootContext()->setContextProperty(QStringLiteral("ArmingGate"), gate);

    // Load the custom component in the fresh engine.
    QString err;
    QDir srcDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(srcDir.filePath(QStringLiteral("custom/qml/MainStatusIndicator.qml")))) {
            break;
        }
        if (!srcDir.cdUp()) break;
    }
    const QString customFile = srcDir.filePath(QStringLiteral("custom/qml/MainStatusIndicator.qml"));
    QVERIFY2(QFileInfo::exists(customFile), qPrintable(QStringLiteral("missing %1").arg(customFile)));
    gateRoot.reset(createComponent(gateEngine, gateWindow.data(), customFile.toUtf8().constData(), &err));
    QVERIFY2(gateRoot, qPrintable(err));

    // Verify gate is active and blocked.
    QVERIFY2(pumpUntilQml([](void *ctx) {
        PreflightManager *mgr = static_cast<PreflightManager *>(ctx);
        return mgr->failedChecks() >= 1;
    }, gatePreflightManager, 5000), "blocking check never evaluated");
    QVERIFY2(!gate->isArmingAllowed(), "gate should be blocked by the forced failure");

    // Create the sensor-status popup and find the Arm button.
    // The popup has broken scope (Drawer.control undefined, etc.) but the
    // Arm button click still works and sets _gateCriticalFails correctly.
    // We INTENTIONALLY LEAK the fresh engine/window/root to avoid the AOT
    // teardown crash (console.log with freed strings in stock components).
    // The process exits cleanly after all tests; OS reclaims memory.
    QQuickItem *armBtn = nullptr;
    const auto components = gateRoot->findChildren<QQmlComponent *>();
    for (QQmlComponent *popupComponent : components) {
        if (!popupComponent->isReady()) continue;
        QObject *popup = popupComponent->create();
        QQuickItem *popupItem = qobject_cast<QQuickItem *>(popup);
        if (!popupItem) {
            if (popup) delete popup;
            continue;
        }
        popupItem->setParentItem(gateWindow->contentItem());
        if (!armBtn) {
            armBtn = findItemByText(popupItem, QStringLiteral("Arm"));
        }
        // Note: we don't clean up popups - they leak with the engine.
    }
    QVERIFY2(armBtn, "Arm button not found in the sensor-status popup");

    // Click the Arm button - the QML handler sets _gateCriticalFails and
    // does NOT call arm() because gate is blocked.
    QVERIFY(QMetaObject::invokeMethod(armBtn, "clicked"));
    pumpQml(200);

    // Verify the gate blocked the arm at the UI level.
    QVERIFY2(QQmlProperty(gateRoot.data(), "_gateCriticalFails").read().toInt() >= 1,
             "blocked arm did not populate the gate-dialog failure count");
    QVERIFY2(!_vehicle->armed(), "blocked arm reached the vehicle — gate routing broken");

    // Explicitly clean up popup items BEFORE base cleanup runs, to destroy
    // the problematic stock components (Drawer, FlightModeMenuIndicator) while
    // the AOT context is still valid. This avoids the teardown crash.
    if (gateWindow && gateWindow->contentItem()) {
        QList<QQuickItem*> children = gateWindow->contentItem()->childItems();
        for (QQuickItem *child : children) {
            child->setParentItem(nullptr);
            delete child;
        }
    }
    gateWindow->contentItem()->setParentItem(nullptr);

    // Clear context properties so no dangling pointers.
    gateEngine->rootContext()->setContextProperty(QStringLiteral("TelemetryProvider"), nullptr);
    gateEngine->rootContext()->setContextProperty(QStringLiteral("PreflightManager"), nullptr);
    gateEngine->rootContext()->setContextProperty(QStringLiteral("ArmingGate"), nullptr);

    // Process events to let deletions complete before base cleanup runs.
    QCoreApplication::processEvents();

    // INTENTIONAL LEAK: gateEngine/gateWindow/gateRoot are function-local statics
    // that persist for process lifetime. Their destructors trigger AOT console.log
    // with freed strings in stock components (Drawer, FlightModeMenuIndicator).
    // The OS reclaims memory on process exit.
    // (gateBridge/gate/etc are still deleted below since they're not QML-owned.)
    delete gateBlockingCheck;
    delete gate;
    delete gatePreflightManager;
    delete gateBridge;
}

// ---------------------------------------------------------------------------
// FlyQuickActionsSmokeTest
// ---------------------------------------------------------------------------
// Phase 1 (RC-primary quad/FW): the Fly view Return action must be present,
// reachable, and wired to exactly ONE function — TelemetryProvider.returnToLaunch() —
// with the old TelemetryProvider.rtl() entry removed so no dual path can
// resurrect. Force Arm / Takeoff removal is asserted in later phases.

static bool hasInvokable(QObject *obj, const char *name)
{
    if (!obj) return false;
    const QMetaObject *mo = obj->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        if (mo->method(i).methodType() != QMetaMethod::Signal && mo->method(i).name() == name) {
            return true;
        }
    }
    return false;
}

class FlyQuickActionsSmokeTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }
    void cleanupTestCase();

    void testLoadsCustomComponent();
    void testReturnWiredToSingleFunction();
    void testOldRtlEntryRemoved();
    void testTakeoffRemoved();

private:
    QSharedPointer<QQmlApplicationEngine> _engine;
    QSharedPointer<QQuickWindow>          _window;
    QSharedPointer<QQuickItem>            _root;
    QGCPalette                           *_palette = nullptr;
    QString                               _filePath;

    void _loadComponent();
};

void FlyQuickActionsSmokeTest::cleanupTestCase()
{
    if (_root) _root->setParentItem(nullptr);
    _root.clear();
    _window.clear();
    _engine.clear();
    delete _palette; _palette = nullptr;
}

void FlyQuickActionsSmokeTest::_loadComponent()
{
    preflightInitOnce();

    if (!_engine) {
        _engine.reset(QGCCorePlugin::instance()->createQmlApplicationEngine(nullptr));
        QVERIFY2(_engine, "createQmlApplicationEngine returned null");
        if (!_palette) _palette = new QGCPalette(this);
        _engine->rootContext()->setContextProperty(QStringLiteral("qgcPal"), _palette);
    }
    if (!_window) {
        _window.reset(createWindow(_engine, 1100, 700));
        QVERIFY(_window);
    }
    if (_root) _root->setParentItem(nullptr);
    _root.clear();
    if (_engine) {
        _engine->collectGarbage();
        pumpQml(25);
    }

    // Load the CUSTOM file from disk (not qrc) so the test exercises exactly the
    // component this product ships; the productMark sentinel proves it is the
    // custom file and not a stock/module-cache shadow.
    QDir srcDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(srcDir.filePath(QStringLiteral("custom/qml/cpts/FlyQuickActions.qml")))) {
            break;
        }
        if (!srcDir.cdUp()) {
            break;
        }
    }
    _filePath = srcDir.filePath(QStringLiteral("custom/qml/cpts/FlyQuickActions.qml"));
    QVERIFY2(QFileInfo::exists(_filePath), qPrintable(QStringLiteral("missing %1").arg(_filePath)));

    QString err;
    _root.reset(createComponent(_engine, _window.data(), _filePath.toUtf8().constData(), &err));
    QVERIFY2(_root, qPrintable(err));

    QCOMPARE(QQmlProperty(_root.data(), "productMark").read().toString(),
             QStringLiteral("FlyQuickActions-custom-rtl2"));
    QVERIFY(findItemByText(_root.data(), QStringLiteral("Return")));
}

void FlyQuickActionsSmokeTest::testLoadsCustomComponent()
{
    _loadComponent();
    // Return button found in _loadComponent(); nothing more to assert here.
}

void FlyQuickActionsSmokeTest::testReturnWiredToSingleFunction()
{
    _loadComponent();

    // The single RTL code path is TelemetryProvider.returnToLaunch(); the QML
    // routes the action through a confirm dialog (two-step), never a direct send.
    QFile f(_filePath);
    QVERIFY2(f.open(QIODevice::ReadOnly), "cannot read FlyQuickActions.qml");
    const QString qml = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(qml.contains(QStringLiteral("TelemetryProvider.returnToLaunch()")),
             "Return action must call the single returnToLaunch() code path");
    QVERIFY2(qml.contains(QStringLiteral("rtlConfirmDialog.open()")),
             "Return action must go through the two-step confirm dialog");
    QVERIFY2(!qml.contains(QStringLiteral("TelemetryProvider.rtl(")),
             "legacy TelemetryProvider.rtl() must not exist (single code path)");
}

void FlyQuickActionsSmokeTest::testOldRtlEntryRemoved()
{
    _loadComponent();

    // The TelemetryProvider exposed to QML must expose returnToLaunch() +
    // distanceToHome() and must NOT expose the old rtl() anymore.
    const QVariant tpVar = _engine->rootContext()->contextProperty(QStringLiteral("TelemetryProvider"));
    QObject *tp = tpVar.value<QObject *>();
    QVERIFY2(tp, "TelemetryProvider context property missing");
    QVERIFY2(hasInvokable(tp, "returnToLaunch"), "TelemetryProvider.returnToLaunch missing");
    QVERIFY2(hasInvokable(tp, "distanceToHome"), "TelemetryProvider.distanceToHome missing");
    QVERIFY2(!hasInvokable(tp, "rtl"), "legacy TelemetryProvider.rtl still exposed");
}

void FlyQuickActionsSmokeTest::testTakeoffRemoved()
{
    _loadComponent();

    // Phase 2: pure deletion of GCS-commanded takeoff from the default Fly view.
    // TelemetryProvider must not expose takeoff() anymore (stock ToolStrip keeps
    // its own takeoff; the custom column is RC-primary).
    const QVariant tpVar = _engine->rootContext()->contextProperty(QStringLiteral("TelemetryProvider"));
    QObject *tp = tpVar.value<QObject *>();
    QVERIFY2(tp, "TelemetryProvider context property missing");
    QVERIFY2(!hasInvokable(tp, "takeoff"), "TelemetryProvider.takeoff still exposed");

    QVERIFY2(!findItemByText(_root.data(), QStringLiteral("Takeoff")),
             "Takeoff QuickButton must be absent from the default Fly view");

    QFile f(_filePath);
    QVERIFY2(f.open(QIODevice::ReadOnly), "cannot read FlyQuickActions.qml");
    const QString qml = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY2(!qml.contains(QStringLiteral("TelemetryProvider.takeoff(")),
             "no takeoff call may remain in the QML");
    QVERIFY2(!qml.contains(QStringLiteral("action:    \"takeoff\"")),
             "no takeoff action wiring may remain in the QML");
}

UT_REGISTER_TEST(MotorCheckPanelSmokeTest)
UT_REGISTER_TEST(MainStatusIndicatorSmokeTest)
UT_REGISTER_TEST(FlyQuickActionsSmokeTest)

#include "QmlSmokeTest.moc"