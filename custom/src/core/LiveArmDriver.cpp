#include "LiveArmDriver.h"

#include <cstdlib>

#include <functional>

#include <QCoreApplication>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>

#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"
#include "TelemetryBridge.h"
#include "core/PreflightManager.h"
#include "managers/FlightSession.h"
#include "core/ArmingGate.h"
#include "core/AbstractCheck.h"

// -- Deterministic "failing critical check" injected into the REAL live
//    PreflightManager so phase A runs against a genuinely closed gate (same
//    mechanism ArmingFlowIntegrationTest uses: a mandatory safety check forced
//    to Failed). Everything downstream of the check — gate arbitration,
//    interceptor install, vehicle veto, wire outcome — is real production code.
namespace {

class LiveArmFailCheck : public AbstractCheck {
public:
    explicit LiveArmFailCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("live.arm.failcheck"),
                        QStringLiteral("Live arm driver failure"),
                        CheckCategory::Safety, CheckType::Auto,
                        true /*mandatory*/, true /*canOverride*/, parent)
    {}
    void evaluate() override {}
    void forceStatus(CheckStatus s, const QString &msg)
    {
        setStatus(s, msg);
    }
};

} // namespace

QObject* LiveArmDriver::findNamedItem(QQuickItem* item, const QString& objectName)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == objectName) {
        return item;
    }
    const QList<QQuickItem*> kids = item->childItems();
    for (QQuickItem* const child : kids) {
        if (QObject* const hit = findNamedItem(child, objectName)) {
            return hit;
        }
    }
    return nullptr;
}

// The app's real main window (the object FlyViewWidgetLayer binds `mainWindow`
// to), located by name rather than assumed.
QObject* LiveArmDriver::_mainWindow()
{
    for (QObject* root : qgcApp()->qmlAppEngine()->rootObjects()) {
        if (QObject* const mw = root->findChild<QObject*>(QStringLiteral("mainWindow"))) {
            return mw;
        }
    }
    // Fall back to a root that exposes showAnalyzeTool.
    for (QObject* root : qgcApp()->qmlAppEngine()->rootObjects()) {
        if (root->metaObject()->indexOfMethod("showAnalyzeTool()") >= 0) {
            return root;
        }
    }
    return nullptr;
}

void LiveArmDriver::install()
{
    // Parented to the app singleton; lives for the whole (short) driver run.
    (void) new LiveArmDriver(qApp);
}

LiveArmDriver::LiveArmDriver(QObject *parent)
    : QObject(parent)
{
    _elapsed.start();
    _timer.setInterval(100);
    connect(&_timer, &QTimer::timeout, this, &LiveArmDriver::_tick);
    _timer.start();

    // Trace WHO ends the event loop: our own Done/Fail or something external
    // (e.g. QML objectCreationFailed). Evidence of the live run's lifecycle.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        _log(QStringLiteral("app aboutToQuit (phase=%1 vehicle=%2 armed=%3 otherQuit=%4)")
                 .arg(int(_phase))
                 .arg(_vehicle ? "yes" : "no")
                 .arg(_vehicle && _vehicle->armed() ? "true" : "false")
                 .arg((_phase == Phase::Done || _phase == Phase::Failed) ? "no" : "YES"));
    });

    _setPhase(Phase::WaitConnect, 120000);

    // Start the loopback-only MockLink so the real MAVLinkProtocol /
    // MultiVehicleManager connect pipeline runs genuinely.
    _mockLink = MockLink::startAPMArduPlaneMockLink(false);
    if (!_mockLink) {
        _fail("failed to start loopback MockLink", 1);
        return;
    }
    _log("loopback MockLink started");
}

void LiveArmDriver::_log(const QString &msg)
{
    qWarning() << "[A3]" << msg;
}

void LiveArmDriver::_setPhase(Phase phase, int timeoutMs)
{
    _phase = phase;
    _phaseDeadlineMs = _elapsed.elapsed() + timeoutMs;
}

void LiveArmDriver::_fail(const QString &msg, int exitCode)
{
    _log("FAIL: " + msg + " (exit=" + QString::number(exitCode) + ")");
    _phase = Phase::Failed;
    _probeTeardown(exitCode);
}

// Deterministic teardown before the app quits. The probe must not exit with a
// live override window (whose "operator force-arm" audit event and 60s window
// would otherwise be left dangling in the gate's state), so we close the window
// first. Then we HARD-EXIT the process: QGC's graceful shutdown (QML engine +
// MockLink + toolbar teardown) intermittently deadlocked the main thread after
// the verdict, racing whichever diagnostic state the probe left behind — the
// queued quit(), deferred singleShot quits, and even fflush()/execl() in this
// multithreaded Qt process were all observed to stall nondeterministically.
// For a diagnostics harness the log evidence is the product; the abrupt
// std::_Exit() after the verdict is deterministic (Qt's logging handler writes
// each message to the log before this runs, so no line is lost). Callers set
// Phase::Done/Failed BEFORE this so the tick stops.
void LiveArmDriver::_probeTeardown(int exitCode)
{
    if (_gate && _gate->isOverrideActive()) {
        _log("teardown: resetGate() closing the override window");
        _gate->resetGate();
    }
    _log(QStringLiteral("probe exit code=%1").arg(exitCode));
    _hardExit(exitCode);
}

// Deterministic process termination for the diagnostics harness. Anything
// fancier than a bare exit-group syscall deadlocks in this multithreaded Qt
// process: fflush(nullptr) and execl() both contend (the glibc loader/stdio
// locks) and stall the process forever (verified repeatedly). A plain
// std::_Exit(exitCode) terminates deterministically here and yields the exact
// status to the caller. Qt's logging handler flushes each message as it is
// written (the verdict lines are all present in the log before this runs), so
// no evidence is lost.
void LiveArmDriver::_hardExit(int exitCode)
{
    std::_Exit(exitCode);
    abort();
}

namespace {
void collectItems(QQuickItem* item, QVector<QQuickItem*>& out, int depth)
{
    if (!item || depth > 80) {
        return;
    }
    out.append(item);
    for (QQuickItem* const child : item->childItems()) {
        collectItems(child, out, depth + 1);
    }
}
}   // namespace

QVector<QQuickItem*> LiveArmDriver::_allQuickItems() const
{
    QVector<QQuickItem*> out;
    for (QObject* const r : qgcApp()->qmlAppEngine()->rootObjects()) {
        auto* const win = qobject_cast<QQuickWindow*>(r);
        if (QQuickItem* const ci = win ? win->contentItem() : qobject_cast<QQuickItem*>(r)) {
            collectItems(ci, out, 0);
        }
    }
    return out;
}

void LiveArmDriver::_tick()
{
    if (_phase == Phase::Failed || _phase == Phase::Done) {
        return;
    }
    if (_elapsed.elapsed() > _phaseDeadlineMs) {
        _fail("phase " + QString::number(int(_phase)) + " timed out", 2);
        return;
    }

    switch (_phase) {
    case Phase::WaitConnect: {
        Vehicle* v = MultiVehicleManager::instance()->activeVehicle();
        if (!v) {
            return;
        }
        _vehicle = v;
        _log("vehicle connected: sysid=" + QString::number(_vehicle->id()) +
             " autopilotLoaded=" + QString::number(int(_vehicle->firmwareType())));
        _setPhase(Phase::WaitInitConnect, 60000);
        return;
    }

    case Phase::WaitInitConnect: {
        if (!_vehicle->isInitialConnectComplete()) {
            return;
        }

        // Resolve the EXACT QML-bound production singletons (the same objects
        // FlyQuickActions.qml / the checklist pages bind to).
        QQmlApplicationEngine* engine = qgcApp()->qmlAppEngine();
        if (!engine) {
            _fail("no QML engine in live boot", 1);
            return;
        }
        _bridge = qobject_cast<TelemetryBridge*>(
            engine->rootContext()->contextProperty(QStringLiteral("TelemetryProvider")).value<QObject*>());
        _preflightManager = qobject_cast<PreflightManager*>(
            engine->rootContext()->contextProperty(QStringLiteral("PreflightManager")).value<QObject*>());
        _gate = qobject_cast<ArmingGate*>(
            engine->rootContext()->contextProperty(QStringLiteral("ArmingGate")).value<QObject*>());

        // The stock Fly-view Return button's controller is reached as
        // globals.guidedControllerFlyView, where `globals` is the QML *id* of
        // MainWindow (src/UI/MainWindow.qml:78) and guidedControllerFlyView is
        // flyView.guidedController (MainWindow.qml:84). An id is not a context
        // property, so resolve it by finding the live GuidedActionsController.
        _guidedController = nullptr;

        if (!_bridge || !_preflightManager || !_gate) {
            _fail(QStringLiteral("production singletons missing bridge=%1 manager=%2 gate=%3")
                      .arg(_bridge ? "ok" : "null")
                      .arg(_preflightManager ? "ok" : "null")
                      .arg(_gate ? "ok" : "null"),
                  1);
            return;
        }

        _log("initial connect complete; " +
             QString::number(_preflightManager->totalChecks()) + " live checks registered");
        _log("QML main window built: " +
             QString::number(engine->rootObjects().size()) + " root object(s)");

        // B10: the built product must carry OUR QML sentinel (the custom layer
        // aliases/overlays stock QML modules; the objectName on the custom
        // FlyQuickActions page proves the shipped ink is the forked file).
        QObject* const productRoot =
            engine->rootObjects().isEmpty()
                ? nullptr
                : engine->rootObjects().constFirst()->findChild<QObject*>(QStringLiteral("FlyQuickActions-custom-rtl2"));
        _log(QStringLiteral("live productMark sentinel: %1 present=%2")
                 .arg(QStringLiteral("FlyQuickActions-custom-rtl2"))
                 .arg(productRoot ? "yes" : "no"));
        if (productRoot) {
            _log(QStringLiteral("live productMark value: %1")
                     .arg(productRoot->property("productMark").toString()));
        }
        _log(QStringLiteral("gate initial state: armingAllowed=%1 overrideActive=%2")
                 .arg(_gate->isArmingAllowed() ? "true" : "false")
                 .arg(_gate->isOverrideActive() ? "true" : "false"));

        // Close the REAL gate deterministically via a failing critical check.
        auto* failCheck = new LiveArmFailCheck(_preflightManager);
        _preflightManager->addCheck(failCheck);
        failCheck->forceStatus(CheckStatus::Failed,
                               QStringLiteral("Live arm driver: simulated critical failure"));
        _log("failing critical check injected");
        _setPhase(Phase::GuidedActionAudit, 20000);
        return;
    }

    

    // Step 0c/0e audit: full GuidedActionsController action-surface survey.
    // Reports EVERY stock guided-action control in the live tree with its real
    // geometry, z-order, visibility and owner chain, so we can tell a genuine
    // second instance from a different view's control, and establish whether
    // FlyViewToolStrip and FlyQuickActions are simultaneously on screen.
    case Phase::GuidedActionAudit: {
        auto* const engine = qgcApp()->qmlAppEngine();

        QQuickItem* contentItem = nullptr;
        for (QObject* root : engine->rootObjects()) {
            if (auto* const win = qobject_cast<QQuickWindow*>(root)) {
                contentItem = win->contentItem();
                break;
            }
        }

        auto ownerChain = [](QQuickItem* item) {
            QStringList parts;
            for (QQuickItem* p = item->parentItem(); p; p = p->parentItem()) {
                parts.prepend(QString::fromLatin1(p->metaObject()->className()));
                if (parts.size() >= 4) {
                    break;
                }
            }
            return parts.join(QStringLiteral(" < "));
        };
        auto geom = [contentItem](QQuickItem* item) {
            if (!contentItem) {
                return QStringLiteral("n/a");
            }
            const QPointF p = item->mapToItem(contentItem, QPointF(0, 0));
            return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
                .arg(p.x(), 0, 'f', 0)
                .arg(p.y(), 0, 'f', 0)
                .arg(item->width(), 0, 'f', 0)
                .arg(item->height(), 0, 'f', 0);
        };

        int takeoffInstances = 0;
        std::function<void(QQuickItem*, int)> walk = [&](QQuickItem* item, int depth) {
            if (!item || depth > 80) {
                return;
            }
            const QString cls = QString::fromLatin1(item->metaObject()->className());
            const bool isStrip = cls.contains(QLatin1String("FlyViewToolStrip"));
            const bool isHoverBtn = cls.contains(QLatin1String("ToolStripHoverButton"));
            const bool isCustomBar = item->objectName() == QLatin1String("FlyQuickActions-custom-rtl2");
            if (isStrip || isHoverBtn || isCustomBar
                || cls.contains(QLatin1String("GuidedActionsController"))) {
                QString label = item->property("text").toString();
                if (label.isEmpty() && item->property("toolStripAction").isValid()) {
                    const QVariant a = item->property("toolStripAction");
                    label = a.value<QObject*>() ? a.value<QObject*>()->property("text").toString()
                                                 : QString();
                }
                if (label == QLatin1String("Takeoff")) {
                    ++takeoffInstances;
                }
                _log(QStringLiteral("AUDIT item: class=%1 title='%2' visible=%3 enabled=%4 "
                                    "opacity=%5 z=%6 %7 | ownerChain: %8")
                         .arg(cls)
                         .arg(label.isEmpty() ? QStringLiteral("-") : label)
                         .arg(item->isVisible() ? "YES" : "no")
                         .arg(item->property("enabled").toBool() ? "YES" : "no")
                         .arg(item->opacity(), 0, 'f', 2)
                         .arg(item->z(), 0, 'f', 0)
                         .arg(geom(item))
                         .arg(ownerChain(item)));
            }
            for (QQuickItem* const child : item->childItems()) {
                walk(child, depth + 1);
            }
        };
        walk(contentItem, 0);

        // Resolve the live GuidedActionsController and dump its invokable
        // signatures, so the wire probes below use a signature that actually
        // matches instead of silently failing.
        {
            std::function<QObject*(QQuickItem*)> findCtl = [&](QQuickItem* item) -> QObject* {
                if (!item) {
                    return nullptr;
                }
                if (QString::fromLatin1(item->metaObject()->className())
                        .startsWith(QLatin1String("GuidedActionsController"))) {
                    return item;
                }
                for (QQuickItem* const child : item->childItems()) {
                    if (QObject* const hit = findCtl(child)) {
                        return hit;
                    }
                }
                return nullptr;
            };
            _guidedController = findCtl(contentItem);
        }
        if (_guidedController) {
            _log(QStringLiteral("SIGNATURE dump: GuidedActionsController resolved, %1 methods")
                     .arg(_guidedController->metaObject()->methodCount()));
            for (int mi = 0; mi < _guidedController->metaObject()->methodCount(); ++mi) {
                _log(QStringLiteral("SIGNATURE [%1] %2")
                         .arg(mi)
                         .arg(QString::fromLatin1(
                             _guidedController->metaObject()->method(mi).methodSignature())));
            }
        } else {
            _log("SIGNATURE dump: GuidedActionsController NOT resolved");
        }

        _log(QStringLiteral("AUDIT takeaway instances found = %1").arg(takeoffInstances));
        _log(QStringLiteral("AUDIT window size = %1x%2")
                 .arg(engine->rootObjects().constFirst()
                          ->property("width").toInt())
                 .arg(engine->rootObjects().constFirst()
                          ->property("height").toInt()));
        // Run the two product-critical verifications FIRST: the stock Return
        // button and the one-shot override token. The long per-action probe loop
        // destabilises teardown, so it runs after these.
        _setPhase(Phase::StockRtlProbe, 40000);
        return;
    }

    case Phase::PanelReach: {
        // The pre-flight checklist page is created lazily (the Analyze tool
        // drawer / toolbar popup builds it a second or two after the window
        // comes up), so POLL rather than sample once.
        auto* const engine = qgcApp()->qmlAppEngine();
        // The engine's root object is a QQuickApplicationWindow (a QQuickWindow,
        // NOT a QQuickItem), so walk from its contentItem.
        QObject* motorPanel = nullptr;
        QObject* surfacePanel = nullptr;
        QObject* viewPanel = nullptr;
        for (QObject* root : engine->rootObjects()) {
            auto* const win = qobject_cast<QQuickWindow*>(root);
            QQuickItem* const walkRoot =
                win ? win->contentItem() : qobject_cast<QQuickItem*>(root);
            if (!motorPanel) {
                motorPanel = findNamedItem(walkRoot, QStringLiteral("preflightMotorCheckPanel"));
            }
            if (!surfacePanel) {
                surfacePanel = findNamedItem(walkRoot, QStringLiteral("preflightGimbalSurfacePanel"));
            }
            if (!viewPanel) {
                viewPanel = findNamedItem(walkRoot, QStringLiteral("preflightChecklistLeftPanel"));
            }
        }
        if (!motorPanel) {
            // Never proceed to arm/regress while the page is still building.
            if (_panelPageSelected && _elapsed.elapsed() < _panelSettleDeadlineMs) {
                return;
            }
            // The checklist page is built only when the operator opens it, so
            // drive the REAL entry point the Fly view exposes rather than
            // waiting for something that never loads on its own.
            if (!_panelActivated) {
                _panelActivated = true;
                // Open the Analyze tool the way the operator does, then select
                // the "Preflight Checklist" page from it. This is the real,
                // shipped entry point (PreflightPlugin::analyzePages registers
                // PreflightChecklistView.qml) — not the one-time wizard.
                // NOTE: the Fly view's openPreflightDialog() is NOT usable here:
                // it reads mainWindow.toolDrawerLoader, a QML id that is
                // undefined across component files, so it bails out. Same dead
                // guard in _attemptOpenPreflightAnalyzePage(). Reported
                // separately; not used as reachability evidence.
                if (!QMetaObject::invokeMethod(_mainWindow(), "showAnalyzeTool")) {
                    _fail("could not invoke MainWindow.showAnalyzeTool()", 1);
                    return;
                }
                _log("PANEL opened the Analyze tool via MainWindow.showAnalyzeTool()");
                _panelSettleDeadlineMs = _elapsed.elapsed() + 4000;
                return;
            }
            if (!_panelPageSelected) {
                if (_elapsed.elapsed() < _panelSettleDeadlineMs) {
                    return;
                }
                _panelPageSelected = true;
                // Choose a session mode FIRST (the Testing button in the real
                // mode dialog), exactly as an operator does before the
                // checklist page loads. FlightSession is a registered QML
                // singleton, so reach it through the C++ singleton, not a
                // context property.
                _log("PANEL choosing session mode 'Testing' via FlightSession::startTestingSession() "
                     "(what the mode dialog's Testing button calls)");
                FlightSession::instance()->startTestingSession();
                QObject* analyzeLoader = nullptr;
                for (QObject* root : engine->rootObjects()) {
                    if (!analyzeLoader) {
                        analyzeLoader = root->findChild<QObject*>(
                            QStringLiteral("analyzePanelLoader"));
                    }
                }
                if (!analyzeLoader) {
                    _fail("Analyze tool did not open (no analyzePanelLoader in the live tree)", 1);
                    return;
                }
                QObject* const analyzeView = analyzeLoader->parent();
                _log(QStringLiteral("PANEL selecting 'Preflight Checklist' in the Analyze tool "
                                    "(page currently: %1)")
                         .arg(analyzeLoader->property("source").toUrl().toString()));
                if (!QMetaObject::invokeMethod(analyzeView, "openPreflightChecklistPage")) {
                    _fail("could not invoke openPreflightChecklistPage() on the Analyze view", 1);
                    return;
                }
                _panelSettleDeadlineMs = _elapsed.elapsed() + 30000;
            }
            if (_elapsed.elapsed() > _panelSettleDeadlineMs) {
                _log(QStringLiteral("PANEL live-tree scan: motorCheckPanel=%1 gimbalSurfacePanel=%2")
                         .arg("MISSING").arg("MISSING"));
                _fail("MotorCheckPanel never appeared in the live pre-flight tree "
                      "(motor-test launcher not reachable from the normal checklist flow)", 1);
            }
            return;
        }
        // Surface-sweep UI lives behind the checklist's "gimbal" view tab, so opening
        // the checklist is not enough — switch to that view like the operator does.
        if (!surfacePanel && !_panelGimbalOpened && viewPanel) {
            _panelGimbalOpened = true;
            // Set the view selector exactly as the operator's "next" button
            // does; the surface loader binds its active state to it.
            _log(QStringLiteral("PANEL switching checklist view to 'gimbal' (from=%1)")
                     .arg(viewPanel->property("_leftView").toString()));
            viewPanel->setProperty("_leftView", QStringLiteral("gimbal"));
        }

        if (!_panelMotorLogged) {
            _panelMotorLogged = true;
            _log(QStringLiteral("PANEL motor sentinel productMark=%1 motorCount=%2 vehicleType=%3")
                     .arg(motorPanel->property("productMark").toString())
                     .arg(motorPanel->property("_mc").toInt())
                     .arg(motorPanel->property("_vt").toString()));
        }

        if (!surfacePanel) {
            // Not yet instantiated (it opens on the "gimbal" view). Drive the
            // real view switch the operator uses, then keep polling.
            if (!_panelActivated) {
                _panelActivated = true;
                for (QObject* root : engine->rootObjects()) {
                    if (QObject* const g = root->findChild<QObject*>(
                            QStringLiteral("preflightGimbalSurfaceLoader"))) {
                        _log("PANEL activating the checklist surface loader "
                             "(source=" + g->property("source").toUrl().toString() + ")");
                        g->setProperty("active", true);
                    }
                }
            }
            if (_elapsed.elapsed() > _panelSettleDeadlineMs) {
                _fail("GimbalTest surface-sweep UI did NOT instantiate from the checklist "
                      "(control-surface sweep not reachable from the normal checklist flow)", 1);
            }
            return;
        }
        _log(QStringLiteral("PANEL surface sentinel productMark=%1")
                 .arg(surfacePanel->property("productMark").toString()));
        _log("PANEL reach: motor test + control-surface sweep BOTH live in the pre-flight tree");
        _setPhase(Phase::WaitGateClosed, 30000);
        return;
    }

    case Phase::WaitGateClosed: {
        if (_gate->isOverrideActive()) {
            return; // parity: an override would defeat the gate-closed assertion
        }
        if (_gate->isArmingAllowed()) {
            return; // wait for the real gate to re-evaluate the failed check
        }
        _log("gate CLOSED (armingAllowed=false, overrideActive=false)");
        _log("denialReason: " + _gate->denialReason());

        // Track the app's own veto verdict: Vehicle emits mavCommandResult
        // DENIED (2) for the blocked ARM_DISARM without anything reaching the
        // wire. This is faster and stronger evidence than a count-only wait.
        _phaseAArmDenied = false;
        _vehicle->disconnect(this); // drop nothing else; we never connected here
        connect(_vehicle, &Vehicle::mavCommandResult, this,
                [this](int vehicleId, int /*targetComponent*/, int command, int ackResult, int /*failureCode*/) {
                    if (command == MAV_CMD_COMPONENT_ARM_DISARM && ackResult == MAV_RESULT_DENIED) {
                        _phaseAArmDenied = true;
                    }
                });

        // Phase A: stock arm through the production bridge object.
        _mockLink->clearReceivedMavCommandCounts();
        _log("-- Phase A -- arm() with gate closed (expect 0 wire commands, DENIED, disarmed)");
        _bridge->arm();
        _setPhase(Phase::PumpPhaseA, 10000);
        return;
    }

    case Phase::PumpPhaseA: {
        const bool deadlineHit = _elapsed.elapsed() >= _phaseDeadlineMs;
        if (!_phaseAArmDenied && !deadlineHit) {
            if (_vehicle->armed()) {
                _fail("phase A: vehicle became ARMED while gate closed — veto failed", 1);
                return;
            }
            return;
        }
        const int countA = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
        _log(QStringLiteral("phase A outcome: armCount=%1 armed=%2 vetoDenied=%3 (expect 0 / false / true)")
                 .arg(countA)
                 .arg(_vehicle->armed() ? "true" : "false")
                 .arg(_phaseAArmDenied ? "true" : "false"));
        if (countA != 0 || _vehicle->armed() || !_phaseAArmDenied) {
            _fail("phase A veto did not hold", 1);
            return;
        }

        // Phase B: operator force-arm (the sanctioned path) — opens the override
        // window and sends exactly one arm command through the interceptor.
        _mockLink->clearReceivedMavCommandCounts();
        _log("-- Phase B -- forceArm() with gate closed (expect 1 wire command, armed)");
        _bridge->forceArm();
        _setPhase(Phase::PumpPhaseB, 30000);
        return;
    }

    case Phase::PumpPhaseB: {
        if (!_vehicle->armed()) {
            return;
        }
        const int countB = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
        const float param1 = _mockLink->lastArmDisarmParam1();
        _log(QStringLiteral("phase B outcome: armCount=%1 lastParam1=%2 armed=%3"
                            " overrideActive=%4 (expect 1 / 1.0 / true / true)")
                 .arg(countB)
                 .arg(param1, 0, 'f', 1)
                 .arg(_vehicle->armed() ? "true" : "false")
                 .arg(_gate->isOverrideActive() ? "true" : "false"));
        if (countB != 1 || !_vehicle->armed()) {
            _fail("phase B force-arm did not produce exactly one arm command", 1);
            return;
        }
        _log("SUCCESS");
        // RTL repro window well short of the phase's hard timeout so the global
        // deadline guard below never fires first; completion is printed inside
        // the case after a short settle time.
        _setPhase(Phase::RtlRepro, 20000);
        return;
    }

    case Phase::RtlRepro: {
        // Evidence-only RTL wire count for Track B (B5 bug repro / B6 fix
        // proof). Does NOT fail the run — the number is what tells the story.
        if (!_rtlReproStarted) {
            _rtlReproStarted = true;
            _mockLink->clearReceivedMavCommandCounts();
            _modeBeforeRtl = _vehicle->flightMode();
            _log("-- RTL repro -- returnToLaunch() with DO_SET_MODE wire counting");
            _log("flightMode before returnToLaunch: " + _modeBeforeRtl);
            const bool rtlResult = _bridge->returnToLaunch();
            _log("returnToLaunch() returned " + QString(rtlResult ? "true" : "false"));
            _rtlSettleDeadlineMs = _elapsed.elapsed() + 1500;
            return;
        }
        if (_elapsed.elapsed() < _rtlSettleDeadlineMs) {
            return;
        }
        const int doSetModeCount = _mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE);
        _log(QStringLiteral("RTL repro outcome: doSetModeWireCount=%1 flightModeBefore=%2"
                            " flightModeAfter=%3")
                 .arg(doSetModeCount)
                 .arg(_modeBeforeRtl)
                 .arg(_vehicle->flightMode()));
        _log(doSetModeCount == 0
                 ? QStringLiteral("RTL repro: ZERO DO_SET_MODE wires (unpatched path bug reproduced)")
                 : QStringLiteral("RTL repro: DO_SET_MODE present on wire"));


        if (!_guidedController) {
            _log("STOCK RTL repro: globals.guidedControllerFlyView NOT found -- cannot drive the "
                 "stock Return button's code path");
            _setPhase(Phase::Done, 5000);
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return;
        }

        // Now drive the SAME outcome through the stock Fly-view Return button's
        // own code path -- GuidedActionsController.executeAction(actionRTL) ->
        // _activeVehicle.guidedModeRTL(). Same DO_SET_MODE counting, so the two
        // paths can be compared directly.
        _log("-- STOCK RTL repro -- invoking GuidedActionsController.executeAction(actionRTL), "
             "the exact call the Fly-view Return button makes");
        _mockLink->clearReceivedMavCommandCounts();
        const bool rtlInvoked = QMetaObject::invokeMethod(
            _guidedController, "executeAction",
            Q_ARG(int, 1),            // actionRTL == 1
            Q_ARG(QVariant, QVariant()),
            Q_ARG(double, 0.0),
            Q_ARG(bool, false));
        _log(QStringLiteral("STOCK RTL invokeMethod(executeAction, actionRTL) returned %1")
                 .arg(rtlInvoked ? "true" : "false"));
        _stockRtlSettleDeadlineMs = _elapsed.elapsed() + 1500;
        _setPhase(Phase::StockRtlRepro, 20000);
        return;
    }

    case Phase::StockRtlRepro: {
        if (_elapsed.elapsed() < _stockRtlSettleDeadlineMs) {
            return;
        }
        const int doSetModeCount = _mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE);
        _log(QStringLiteral("STOCK RTL outcome: doSetModeWireCount=%1 flightModeBefore=%2"
                            " flightModeAfter=%3")
                 .arg(doSetModeCount)
                 .arg(_modeBeforeRtl)
                 .arg(_vehicle->flightMode()));
        if (doSetModeCount == 0) {
            _log("STOCK RTL REGRESSION CONFIRMED: the live Fly-view Return button sends NOTHING "
                 "on the wire while the fixed TelemetryBridge::returnToLaunch() path sent 1. "
                 "Phase 4 marked RTL fixed, but the stock button never routes through it.");
        } else {
            _log("STOCK RTL: DO_SET_MODE present on wire (stock button happens to work on this "
                 "firmware plugin)");
        }
        _setPhase(Phase::Done, 5000);
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
        return;
    }

    // Step 0c.2: wire-count every GuidedActionsController action that routes
    // through Vehicle/FirmwarePlugin. Each is invoked through
    // executeAction() -- the exact call the on-screen button makes -- with the
    // MockLink command counters cleared immediately before, so a count of 0
    // proves the action reached the UI dispatch and then sent NOTHING.
    case Phase::StockRtlProbe: {
        if (!_stockRtlSent) {
            _stockRtlSent = true;
            _mockLink->clearReceivedMavCommandCounts();
            _log("-- STOCK Return button: driving GuidedActionRTL -> confirmAction(actionRTL) "
                 "-- must now send MAV_CMD_DO_SET_MODE (was 0 before the fork patch) --");
            const bool invoked = QMetaObject::invokeMethod(
                _guidedController, "executeAction",
                Q_ARG(QVariant, QVariant(1)),       // actionRTL
                Q_ARG(QVariant, QVariant()),
                Q_ARG(QVariant, QVariant()),
                Q_ARG(QVariant, QVariant(false)));  // optionChecked
            _log(QStringLiteral("STOCKRTL invoked=%1").arg(invoked ? "true" : "false"));
            _probeSettleDeadlineMs = _elapsed.elapsed() + 3000;
            return;
        }
        if (_elapsed.elapsed() < _probeSettleDeadlineMs) {
            return;
        }
        const int rtlCount = _mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE);
        _log(QStringLiteral("STOCKRTL OUTCOME: doSetModeWireCount=%1")
                 .arg(rtlCount));
        _log(rtlCount > 0
                 ? QStringLiteral("STOCKRTL VERDICT: PASS -- the stock Return button reaches the wire")
                 : QStringLiteral("STOCKRTL VERDICT: FAIL -- stock Return still sends nothing"));
        // The per-action survey is already complete and its data collected; the
        // buttons it exercised have been removed. Go to the Force Arm / one-shot
        // token verification, which must not be starved by a long probe loop.
        _setPhase(Phase::ForceArmSignalProbe, 90000);
        _mockLink->clearReceivedMavCommandCounts();
        _vehicle->setArmed(false, false);
        _log("-- Disarmed for the Force Arm probe; gate is still CLOSED --");
        _forceArmDeadlineMs = _elapsed.elapsed() + 1500;
        return;
    }
    case Phase::ActionWireProbe: {
        struct Probe { int action; const char* name; MAV_CMD cmd; const char* cmdName; };
        static const Probe kProbes[] = {
            // POSITIVE CONTROL first: Disarm does NOT go through FirmwarePlugin,
            // so it must reach the wire. If this counts 0 the probe mechanism is
            // broken and every other zero would be meaningless.
            { 5,  "POSCTRL-Disarm", MAV_CMD_COMPONENT_ARM_DISARM, "MAV_CMD_COMPONENT_ARM_DISARM"},
            { 11, "LandAbort",      MAV_CMD_DO_GO_AROUND,       "MAV_CMD_DO_GO_AROUND"},
            { 26, "Gripper",        MAV_CMD_DO_SET_SERVO,       "MAV_CMD_DO_SET_SERVO"},
        };
        constexpr int kProbeCount = int(sizeof(kProbes) / sizeof(kProbes[0]));

        // Arm the vehicle first: most guided actions are gated on armed/flying.
        if (_probeIndex > 0 && !_vehicle->armed() && !_probeReArmed) {
            _probeReArmed = true;
            _bridge->forceArm();
            _probeSettleDeadlineMs = _elapsed.elapsed() + 1500;
            return;
        }
        if (_probeIndex > 0 && _probeReArmed && !_vehicle->armed()
            && _elapsed.elapsed() < _probeSettleDeadlineMs) {
            return;
        }
        if (_probeIndex > 0 && _probeReArmed && _vehicle->armed()) {
            _probeReArmed = false;
        }
        if (_probeIndex == 0 && !_probeArmed) {
            _probeArmed = true;
            if (!_probeResultConnected) {
                _probeResultConnected = true;
                // mavCommandResult(vehicleId, targetComponent, command, ackResult, failureCode)
                connect(_vehicle, &Vehicle::mavCommandResult, this,
                        [this](int, int, int command, int ackResult, int failureCode) {
                            _probeResults.append({command, ackResult, failureCode});
                        });
            }
            _bridge->forceArm();
            _probeSettleDeadlineMs = _elapsed.elapsed() + 1500;
            _log("-- Action wire probe -- arming via TelemetryBridge::forceArm() so "
                 "armed/flying-gated actions are actually eligible --");
            return;
        }
        if (_probeIndex == 0 && _elapsed.elapsed() < _probeSettleDeadlineMs) {
            return;
        }
        // Re-arm before the positive control so it has something to disarm.
        if (_probeIndex == kProbeCount - 1 && !_probeReArmed) {
            _probeReArmed = true;
            _bridge->forceArm();
            _probeSettleDeadlineMs = _elapsed.elapsed() + 1500;
            return;
        }
        if (_probeIndex == kProbeCount - 1 && _elapsed.elapsed() < _probeSettleDeadlineMs) {
            return;
        }
        if (_probeIndex >= kProbeCount) {
            _log(QStringLiteral("PROBE LOOP ENDING: vehicle armed=%1 -- if false, every "
                                "armed-gated probe above was invalid")
                     .arg(_vehicle->armed() ? "true" : "false"));
            _setPhase(Phase::Done, 5000);
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return;
        }

        const Probe& pr = kProbes[_probeIndex];
        if (!_probeStarted) {
            _probeStarted = true;
            // Clear BEFORE the action fires so emissions during the settle window
            // are the ones this probe caused.
            _probeResults.clear();
            _mockLink->clearReceivedMavCommandCounts();
            _log(QStringLiteral("PROBE %1 (actionCode=%2): invoking executeAction via "
                                "GuidedActionsController, counting %3 on the wire")
                     .arg(pr.name)
                     .arg(pr.action)
                     .arg(QString::fromLatin1(pr.cmdName)));
            // QML infers executeAction's parameter types from usage:
            //   actionCode        -> int        (switched against int constants)
            //   actionData        -> QVariant   (var, passed straight through)
            //   sliderOutputValue -> double     (used in unit-conversion arithmetic)
            //   optionChecked     -> bool
            // Invoking with the wrong signature silently does nothing, so the
            // return value is logged and a POSCTRL probe guards the whole table.
                        const bool invoked = QMetaObject::invokeMethod(
                _guidedController, "executeAction",
                Q_ARG(QVariant, QVariant(pr.action)),
                Q_ARG(QVariant, QVariant()),
                Q_ARG(QVariant, QVariant(10.0)),
                Q_ARG(QVariant, QVariant(false)));
            _log(QStringLiteral("PROBE %1: executeAction invoked=%2 armed=%3")
                     .arg(pr.name)
                     .arg(invoked ? "true" : "false")
                     .arg(_vehicle->armed() ? "true" : "false"));
            if (!invoked) {
                _log(QStringLiteral("PROBE %1: executeAction invokeMethod FAILED -- this "
                                    "probe's counts are invalid")
                         .arg(pr.name));
                for (int mi = 0; mi < _guidedController->metaObject()->methodCount(); ++mi) {
                    const QMetaMethod mm = _guidedController->metaObject()->method(mi);
                    const QByteArray sig = mm.methodSignature();
                    if (sig.startsWith("executeAction") || sig.startsWith("confirmAction")) {
                        _log(QStringLiteral("PROBE signature available: %1")
                                 .arg(QString::fromLatin1(sig)));
                    }
                }
            }
            _probeSettleDeadlineMs = _elapsed.elapsed() + 1200;
            return;
        }
        _probeStarted = false;
        // Capture the command-result signal so a zero wire count can be
        // attributed: no signal at all means the action never attempted to send
        // (base no-op / capability guard); a FAILED/DUPLICATE result means it
        // tried and was rejected locally.
        const int count = _mockLink->receivedMavCommandCount(pr.cmd);
        const int anyCount = _mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_MODE)
                           + _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM)
                           + _mockLink->receivedMavCommandCount(MAV_CMD_NAV_TAKEOFF)
                           + _mockLink->receivedMavCommandCount(MAV_CMD_DO_GO_AROUND)
                           + _mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO)
                           + _mockLink->receivedMavCommandCount(MAV_CMD_MISSION_START);
        _log(QStringLiteral("PROBE RESULT %1: expected=%2 count=%3 otherGuidedCmds=%4 -> %5")
                 .arg(pr.name)
                 .arg(QString::fromLatin1(pr.cmdName))
                 .arg(count)
                 .arg(anyCount)
                 .arg(count == 0 ? QStringLiteral("SENDS NOTHING (base no-op)")
                                 : QStringLiteral("sends on the wire")));
        QString attempt;
        for (const QList<QVariant>& r : _probeResults) {
            attempt += QStringLiteral(" [cmd=%1 result=%2 failCode=%3]")
                           .arg(r.at(0).toInt())
                           .arg(r.at(1).toInt())
                           .arg(r.at(2).toInt());
        }
        _log(QStringLiteral("PROBE ATTEMPT %1: mavCommandResult emissions=%2%3")
                 .arg(pr.name)
                 .arg(_probeResults.count())
                 .arg(attempt));
        ++_probeIndex;
        return;
    }

    // Step 0d: Force Arm through the REAL signal chain.
    // mainWindow.forceArmVehicleRequest -> GuidedActionsController
    // .onForceArmVehicleRequest -> confirmAction(actionForceArm) ->
    // confirmDialog -> operator accept -> executeAction(actionForceArm)
    // -> _activeVehicle.forceArm(). Gate is deliberately CLOSED here.
    case Phase::ForceArmSignalProbe: {
        if (_forceArmPressed) {
            if (_elapsed.elapsed() < _forceArmDeadlineMs) {
                return;
            }
            const int armCount = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
            _log(QStringLiteral("FORCEARM OUTCOME: armDisarmWireCount=%1 param2ForceArmMag=%2 "
                                "armedBefore=%3 armedAfter=%4 gateArmingAllowed=%5 "
                                "gateOverrideActive=%6")
                     .arg(armCount)
                     .arg(_mockLink->lastArmDisarmParam2())
                     .arg(_armedBeforeStockForceArm ? "true" : "false")
                     .arg(_vehicle->armed() ? "true" : "false")
                     .arg(_gate->isArmingAllowed() ? "true" : "false")
                     .arg(_gate->isOverrideActive() ? "true" : "false"));
            _log(armCount == 0
                     ? QStringLiteral("FORCEARM VERDICT: stock entry point sent NOTHING with the "
                                      "gate closed -- already vetoed, UX cleanup only")
                     : QStringLiteral("FORCEARM VERDICT: GENUINE BYPASS -- the stock entry point "
                                      "armed the vehicle with the gate CLOSED"));

            // Decisive test of the Vehicle-level method itself, independent of
            // any QML: call Vehicle::forceArm() directly with the gate closed.
            // Vehicle.cc has no ArmingGate reference anywhere, so if this sends
            // on the wire the chokepoint does NOT cover the stock entry point.
            _log("FORCEARM DIRECT: calling _vehicle->forceArm() with the gate still CLOSED "
                 "(no QML involved)");
            _mockLink->clearReceivedMavCommandCounts();
            _armedBeforeStockForceArm = _vehicle->armed();
            _vehicle->forceArm();
            _forceArmDeadlineMs = _elapsed.elapsed() + 4000;
            _forceArmDirect = true;
            _forceArmPressed = false;
            return;
        }
        if (_forceArmDirect && !_forceArmDirectLogged) {
            if (_elapsed.elapsed() < _forceArmDeadlineMs) {
                return;
            }
            _forceArmDirectLogged = true;
            const int armCount = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
            _log(QStringLiteral("FORCEARM DIRECT (settled 4000ms to clear the "
                                "duplicate-command guard) OUTCOME: armDisarmWireCount=%1 "
                                "param1Arm=%2 param2ForceArmMagic=%3 armedBefore=%4 armedAfter=%5")
                     .arg(armCount)
                     .arg(_mockLink->lastArmDisarmParam1())
                     .arg(_mockLink->lastArmDisarmParam2())
                     .arg(_armedBeforeStockForceArm ? "true" : "false")
                     .arg(_vehicle->armed() ? "true" : "false"));
            _log(armCount == 0
                     ? QStringLiteral("FORCEARM DIRECT VERDICT: Vehicle::forceArm() was BLOCKED "
                                      "with the gate closed -- the chokepoint covers the "
                                      "ungated path too")
                     : QStringLiteral("FORCEARM DIRECT VERDICT: REGRESSION -- Vehicle::forceArm() "
                                      "reached the wire with param2=%1 despite the closed gate")
                          .arg(_mockLink->lastArmDisarmParam2()));
        }
        if (_forceArmDirect && _forceArmDirectLogged) {
            // --- One-shot CALLER-SCOPED override proof ---
            // A legitimate bench override (TelemetryBridge::forceArm) opens the
            // 60s window and mints a one-shot token for ITS OWN command. A
            // subsequent unattributed arm inside that same window — the terminal
            // of mainWindow.forceArmVehicleRequest, Vehicle::forceArm(), which is
            // parametrically identical (param1=1 param2=2989) — must be DENIED by
            // the interceptor. Pre-fix this window was AMBIENT: the interceptor's
            // first branch short-circuits on isArmingAllowed() (which forceArm()
            // flips true for the whole span) and the stock terminal reaches the
            // wire. The denial is distinguished from a dropped duplicate by the
            // MAV_RESULT_DENIED mavCommandResult the interceptor emits.
            if (!_ambResultConnected) {
                _ambResultConnected = true;
                connect(_vehicle, &Vehicle::mavCommandResult, this,
                        [this](int, int, int command, int ackResult, int) {
                            if (_ambWaitActive && command == MAV_CMD_COMPONENT_ARM_DISARM &&
                                ackResult == MAV_RESULT_DENIED) {
                                _ambDeniedSeen = true;
                            }
                        });
            }
            if (!_overrideWindowOpened) {
                _overrideWindowOpened = true;
                _ambWaitActive = false;
                _ambDeniedSeen = false;
                _mockLink->clearReceivedMavCommandCounts();
                _log("OVERRIDE TEST: opening the sanctioned bench override once via "
                     "TelemetryBridge::forceArm() (mints the one-shot token; its own "
                     "command consumes it)");
                _bridge->forceArm();
                // Long settle: the sanctioned send's ACK must fully clear the
                // Vehicle duplicate-command guard so the stock-terminal count below
                // can only be zero because the interceptor vetoed, never because the
                // send was dropped as a pending duplicate.
                _forceArmDeadlineMs = _elapsed.elapsed() + 3000;
                return;
            }
            if (!_overrideSanctionedLogged) {
                if (_elapsed.elapsed() < _forceArmDeadlineMs) {
                    return;
                }
                const int w = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
                _log(QStringLiteral("OVERRIDE sanctioned send: wireCount=%1 param2=%2 armed=%3 "
                                    "overrideActive=%4 windowSec=%5")
                         .arg(w)
                         .arg(_mockLink->lastArmDisarmParam2())
                         .arg(_vehicle->armed() ? "true" : "false")
                         .arg(_gate->isOverrideActive() ? "true" : "false")
                         .arg(kForceArmOverrideSec));
                _overrideSanctionedLogged = true;
                _overrideStockLogged = false;
                // The verdict below only means caller-scope if the window is in
                // fact STILL OPEN here. If it closed for an unrelated reason (a
                // stray disarm resets the gate), the denial would come from the
                // closed-gate path, not from caller-scope — that must not be
                // reported as a caller-scope PASS.
                if (!_gate->isOverrideActive()) {
                    _log("OVERRIDE VERDICT: INCONCLUSIVE -- the override window is no longer "
                         "active when the stock terminal is attempted; cannot prove "
                         "caller-scope");
                    _setPhase(Phase::Done, 250);
                    _probeTeardown(0);
                    return;
                }
                _ambWaitActive = true;
                _ambDeniedSeen = false;
                _mockLink->clearReceivedMavCommandCounts();
                _log("OVERRIDE TEST: attempting the mainWindow.forceArmVehicleRequest terminal "
                     "(Vehicle::forceArm(), param1=1 param2=2989) INSIDE the still-open window -- "
                     "must be DENIED when the override is caller-scoped");
                _vehicle->forceArm();
                _forceArmDeadlineMs = _elapsed.elapsed() + 3000;
                return;
            }
            if (!_overrideStockLogged) {
                if (_elapsed.elapsed() < _forceArmDeadlineMs) {
                    return;
                }
                _overrideStockLogged = true;
                _ambWaitActive = false;
                const int w = _mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM);
                _log(QStringLiteral("OVERRIDE stock-terminal inside window: wireCount=%1 param2=%2 "
                                    "interceptorDenied=%3 armed=%4 overrideActive=%5")
                         .arg(w)
                         .arg(_mockLink->lastArmDisarmParam2())
                         .arg(_ambDeniedSeen ? "YES" : "no")
                         .arg(_vehicle->armed() ? "true" : "false")
                         .arg(_gate->isOverrideActive() ? "true" : "false"));
                if (w == 0 && _ambDeniedSeen) {
                    _log("OVERRIDE VERDICT: PASS CALLER-SCOPE -- the stock terminal was DENIED "
                         "inside the window (one-shot, caller-scoped token holds)");
                } else if (w > 0) {
                    _log("OVERRIDE VERDICT: AMBIENT BYPASS REPRODUCED -- the stock terminal "
                         "reached the wire inside the window; the override is NOT caller-scoped");
                } else {
                    _log("OVERRIDE VERDICT: INCONCLUSIVE -- no wire count AND no interceptor "
                         "DENIED either; the command was dropped, not vetoed, so this is not "
                         "evidence of caller-scope");
                }
                _setPhase(Phase::Done, 5000);
                _probeTeardown(0);
                return;
            }
            return;
        }
        QObject* const mw = _mainWindow();
        if (!_forceArmSignalSent && !_forceArmWaitLogged) {
            _forceArmWaitLogged = true;
            _armedBeforeStockForceArm = _vehicle->armed();
            _log(QStringLiteral("FORCEARM waiting 3000ms for a disarmed+settled state: armed=%1")
                     .arg(_armedBeforeStockForceArm ? "true" : "false"));
            _forceArmDeadlineMs = _elapsed.elapsed() + 3000;
            return;
        }
        if (!_forceArmSignalSent) {
            _armedBeforeStockForceArm = _vehicle->armed();
            _log(QStringLiteral("FORCEARM settled: armed=%1 -- emitting "
                                "mainWindow.forceArmVehicleRequest(), the real stock signal")
                     .arg(_armedBeforeStockForceArm ? "true" : "false"));
            _mockLink->clearReceivedMavCommandCounts();
            if (!_gate->isArmingAllowed() && !_gate->isOverrideActive()) {
                _log(QStringLiteral("FORCEARM precondition: gate CLOSED armingAllowed=false "
                                    "overrideActive=false"));
            }
            _forceArmSignalSent = QMetaObject::invokeMethod(mw, "forceArmVehicleRequest");
            _log(QStringLiteral("FORCEARM signal emitted=%1")
                     .arg(_forceArmSignalSent ? "true" : "false"));
            _forceArmWaitLogged = false;
            _forceArmDeadlineMs = _elapsed.elapsed() + 2500;   // dialog show timer is 1000ms
            return;
        }
        if (_elapsed.elapsed() < _forceArmDeadlineMs) {
            return;
        }
        // The confirm dialog should now be visible; press its accept affordance
        // exactly as the operator would.
        // Only accept via the SliderSwitch that belongs to the GuidedActionConfirm
        // dialog, otherwise we may be pressing an unrelated slider.
        QQuickItem* slider = nullptr;
        std::function<bool(QQuickItem*)> inConfirmDialog = [](QQuickItem* item) {
            for (QQuickItem* p = item->parentItem(); p; p = p->parentItem()) {
                if (QString::fromLatin1(p->metaObject()->className())
                        .contains(QLatin1String("GuidedActionConfirm"))) {
                    return true;
                }
            }
            return false;
        };
        std::function<void(QQuickItem*)> find = [&](QQuickItem* item) {
            if (!item || slider) {
                return;
            }
            if (QString::fromLatin1(item->metaObject()->className())
                    .contains(QLatin1String("SliderSwitch"))
                && inConfirmDialog(item)) {
                slider = item;
                return;
            }
            for (QQuickItem* const child : item->childItems()) {
                find(child);
            }
        };
        for (QObject* const r : qgcApp()->qmlAppEngine()->rootObjects()) {
            if (auto* const w = qobject_cast<QQuickWindow*>(r)) {
                find(w->contentItem());
            }
        }
        // Report the dialog state so a zero count cannot be mistaken for a
        // chain that silently never executed.
        for (QQuickItem* it : _allQuickItems()) {
            if (QString::fromLatin1(it->metaObject()->className())
                    .contains(QLatin1String("GuidedActionConfirm"))) {
                _log(QStringLiteral("FORCEARM confirmDialog: visible=%1 action=%2 (24==actionForceArm)")
                         .arg(it->isVisible() ? "YES" : "no")
                         .arg(it->property("action").toInt()));
            }
        }
        if (!slider) {
            _log("FORCEARM SliderSwitch (confirm affordance) NOT found in the live tree");
        }
        _mockLink->clearReceivedMavCommandCounts();
        _armedBeforeStockForceArm = _vehicle->armed();
        if (slider) {
            _log(QStringLiteral("FORCEARM pressing the confirm dialog's accept affordance "
                                "(SliderSwitch.accept()); armed=%1 gateArmingAllowed=%2 "
                                "gateOverrideActive=%3")
                     .arg(_armedBeforeStockForceArm ? "true" : "false")
                     .arg(_gate->isArmingAllowed() ? "true" : "false")
                     .arg(_gate->isOverrideActive() ? "true" : "false"));
            QMetaObject::invokeMethod(slider, "accept");
        }
        _forceArmDeadlineMs = _elapsed.elapsed() + 3000;
        _forceArmPressed = true;
        return;
    }

    default:
        return;
    }
}