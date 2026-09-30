#pragma once

#include <QObject>
#include <QElapsedTimer>

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

public:
    static void install();

private slots:
    void _tick();

private:
    enum class Phase {
        WaitConnect = 0,
        WaitInitConnect,
        WaitGateClosed,
        PumpPhaseA,
        PumpPhaseB,
        RtlRepro,
        Done,
        Failed
    };

    explicit LiveArmDriver(QObject *parent = nullptr);

    void _log(const QString &msg);
    void _setPhase(Phase phase, int timeoutMs);
    void _fail(const QString &msg, int exitCode);

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
    QTimer _timer;
};