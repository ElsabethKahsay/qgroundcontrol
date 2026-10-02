#pragma once

#include <QObject>
#include <QElapsedTimer>

class QObject;
class QQuickItem;

class MockLink;
class Vehicle;
class TelemetryBridge;
class PreflightManager;
class ArmingGate;

/// Hidden debug-only driver hook enabled by the `--live-arm` command line flag.
///
/// Boots the REAL app normally (runningUnitTests() == false, real
/// PreflightPlugin::init, real QML root) inside whatever network the process
/// runs in — the operator is expected to launch it inside a loopback-only
/// namespace so no external MAVLink source can join. Once the loopback MockLink
/// vehicle appears the driver drives the REAL production arm pathway end-to-end:
///
///   phase A: real closed gate (mandatory safety check forced to Failed)
///            -> TelemetryBridge::arm() vetoed locally by the Vehicle's arm
///            interceptor (installed as a PreflightPlugin::_setupForVehicle
///            side effect): zero MAV_CMD_COMPONENT_ARM_DISARM on the wire,
///            vehicle stays disarmed.
///   phase B: real operator force-arm (TelemetryBridge::forceArm) opens the
///            gate override window, the arm goes out EXACTLY once, vehicle arms.
///   RTL repro (B5/B6): `--live-arm` also invokes TelemetryBridge::returnToLaunch
///            in the armed state and counts MAV_CMD_DO_SET_MODE on the wire —
///            evidence-only (no pass/fail), so the same driver shows the bug
///            on the unpatched path (0 wires) and the fix on the patched path
///            (1 wire via MAV_CMD_DO_SET_MODE).
///
/// Evidence is printed as grep-able `[A3]` lines plus the vehicle/gate/mock
/// state, then the app exits 0 (or non-zero on failure). Debug builds only.
class LiveArmDriver : public QObject {
    Q_OBJECT

    // Depth-first search of the VISUAL (QQuickItem) tree for a named item.
    // QML Items that get reparented — which every Loader/overlay-hosted item is
    // — are not QObject children of their visual parent, so QObject::findChild()
    // misses them even while they are live and logging their sentinels.
    static QObject* findNamedItem(QQuickItem* item, const QString& objectName);

public:
    static void install();

private slots:
    void _tick();

private:
    static QObject* _mainWindow();
    QVector<QQuickItem*> _allQuickItems() const;

private:
    enum class Phase {
        WaitConnect = 0,
        WaitInitConnect,
        GuidedActionAudit,
        PanelReach,
        WaitGateClosed,
        PumpPhaseA,
        PumpPhaseB,
        RtlRepro,
        StockRtlRepro,
        ActionWireProbe,
        StockRtlProbe,
        ForceArmSignalProbe,
        Done,
        Failed
    };

    explicit LiveArmDriver(QObject *parent = nullptr);

    void _log(const QString &msg);
    void _setPhase(Phase phase, int timeoutMs);
    void _fail(const QString &msg, int exitCode);
    void _probeTeardown(int exitCode = 0);
    void _hardExit(int exitCode);

    Vehicle* _vehicle = nullptr;
    MockLink* _mockLink = nullptr;
    TelemetryBridge* _bridge = nullptr;
    PreflightManager* _preflightManager = nullptr;
    ArmingGate* _gate = nullptr;

    Phase _phase = Phase::WaitConnect;
    QElapsedTimer _elapsed;
    qint64 _phaseDeadlineMs = 0;
    bool _phaseAArmDenied = false;
    bool _rtlReproStarted = false;
    QString _modeBeforeRtl;
    qint64 _rtlSettleDeadlineMs = 0;
    qint64 _stockRtlSettleDeadlineMs = 0;
    QObject* _guidedController = nullptr;   // globals.guidedControllerFlyView

    // Step 0c.2 per-action wire probe
    int         _probeIndex = 0;
    bool        _probeStarted = false;
    bool        _probeArmed = false;
    bool        _probeResultConnected = false;
    bool        _probeReArmed = false;
    QList<QList<QVariant>> _probeResults;
    qint64      _probeSettleDeadlineMs = 0;

    // Step 0d Force Arm real-signal-chain probe
    bool        _forceArmSignalSent = false;
    bool        _forceArmPressed = false;
    bool        _forceArmDirect = false;
    bool        _forceArmDirectLogged = false; // direct verdict logged once (the override stages share this tick path)
    bool        _forceArmWaitLogged = false;
    bool        _stockRtlSent = false;
    bool        _armedBeforeStockForceArm = false;
    qint64      _forceArmDeadlineMs = 0;

    // Step 0e: one-shot CALLER-SCOPED override proof. A legitimate bench override
    // (TelemetryBridge::forceArm) mints a token for ITS OWN command; a subsequent
    // unattributed arm in the same window (the mainWindow.forceArmVehicleRequest
    // terminal, Vehicle::forceArm()) must be DENIED. Pre-fix the window is ambient
    // and the stock terminal reaches the wire.
    bool        _overrideWindowOpened = false;   // sanctioned override send settled
    bool        _overrideSanctionedLogged = false; // sanctioned count logged
    bool        _overrideStockLogged = false;    // stock-terminal verdict logged
    bool        _ambWaitActive = false;          // capture DENIED for ARM in-window
    bool        _ambDeniedSeen = false;          // interceptor DENIED observed
    bool        _ambResultConnected = false;     // mavCommandResult capture wired once
    QObject* _panelRoot = nullptr;
    bool _panelMotorLogged = false;
    bool _panelActivated = false;
    bool _panelGimbalOpened = false;
    bool _panelPageSelected = false;
    bool _panelModeChosen = false;
    qint64 _panelModeDeadlineMs = 0;
    qint64 _panelSettleDeadlineMs = 0;
    QTimer _timer;
};