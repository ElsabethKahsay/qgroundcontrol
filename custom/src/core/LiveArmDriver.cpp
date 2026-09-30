#include "LiveArmDriver.h"

#include <QCoreApplication>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"
#include "Comms/MockLink/MockLink.h"
#include "TelemetryBridge.h"
#include "core/PreflightManager.h"
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
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    qApp->exit(exitCode);
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
        _setPhase(Phase::Done, 5000);
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
        return;
    }

    default:
        return;
    }
}