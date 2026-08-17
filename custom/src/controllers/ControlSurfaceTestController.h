#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QPair>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QString>

#include "QGCMAVLink.h"

class Vehicle;

struct ControlSurface {
    QString id;        // "elevon_l", "elevon_r", "aileron", "elevator", "rudder", "flap", "nose_wheel"
    QString label;     // "Left Elevon", "Right Elevon", "Aileron", ...
    int     channel = 1;      // Primary MAVLink servo output channel (1-16)
    int     channel2 = -1;    // Secondary servo output channel for dual-servo direct drive (-1 = unused)
    int     channel2Invert = 0; // 1 = invert channel2 PWM relative to channel (aileron differential)
    int     rcChannel = -1;   // primary RC channel (RCMAP) that drives this surface, -1 for DO_SET_SERVO
    int     rcChannel2 = -1;  // second RC channel used for elevon isolation, -1 if unused
    int     elevonMode = 0;   // 0 = single RC channel, 1 = left-elevon isolation, 2 = right-elevon isolation
    QString question;         // operator prompt shown after the sweep for direction confirmation
    int     centerPwm = 1500; // 1500
    int     minPwm = 988;     // read from SERVOn_MIN
    int     maxPwm = 2011;    // read from SERVOn_MAX
};

class ControlSurfaceTestController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList surfaces READ surfaces NOTIFY surfacesChanged)
    Q_PROPERTY(int activeSurface READ activeSurface NOTIFY activeSurfaceChanged)
    Q_PROPERTY(QVariantList surfaceStates READ surfaceStates NOTIFY surfaceStatesChanged)
    Q_PROPERTY(bool isArmed READ isArmed NOTIFY isArmedChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorMessageChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(int sweptPwm READ sweptPwm NOTIFY sweptPwmChanged)
    Q_PROPERTY(int sweptPwm2 READ sweptPwm2 NOTIFY sweptPwmChanged)
    Q_PROPERTY(QString servoOutputsRaw READ servoOutputsRaw NOTIFY servoOutputsRawChanged)
    Q_PROPERTY(QString sweepVerdict READ sweepVerdict NOTIFY sweepVerdictChanged)
    Q_PROPERTY(bool swapElevonLR READ swapElevonLR WRITE setSwapElevonLR NOTIFY swapElevonLRChanged)

public:
    enum SurfaceState {
        Idle = 0,
        Sweeping = 1,
        Pass = 2,
        Fail = 3,
        Cooldown = 4
    };
    Q_ENUM(SurfaceState)

    explicit ControlSurfaceTestController(QObject *parent = nullptr);
    ~ControlSurfaceTestController() override = default;

    void setVehicle(Vehicle *vehicle);

    QVariantList surfaces() const;
    int activeSurface() const { return m_activeSurface; }
    QVariantList surfaceStates() const;
    bool isArmed() const { return m_isArmed; }
    QString lastErrorMessage() const { return m_lastErrorMessage; }
    QString flightMode() const { return m_flightMode; }

    /// When true, the left/right elevon isolation mapping is swapped so the
    /// operator can compensate for crossed SERVO1/SERVO2 wiring without moving
    /// plugs. Persisted in QSettings.
    bool swapElevonLR() const { return m_swapElevonLR; }
    void setSwapElevonLR(bool swap);

    /// Live SERVO_OUTPUT_RAW PWM on the active surface's servo output channel
    /// (0 when no feedback is being received).  Lets the operator tell a dead
    /// servo/wiring from the flight controller ignoring the input (e.g. the
    /// yaw stick is ignored by autopilot modes like LOITER).
    int sweptPwm() const { return m_sweptPwm; }

    /// Live SERVO_OUTPUT_RAW PWM on the active surface's SECONDARY servo output
    /// channel (channel2, e.g. the other elevon).  Lets the operator see that the
    /// flight controller is driving BOTH elevon channels even when one of the
    /// physical servos does not respond (dead servo / broken wire), so a "only
    /// the left side moves" report can be told apart from an FC routing problem.
    int sweptPwm2() const { return m_sweptPwm2; }

    /// "CH1=1500 CH2=1500 CH3=1500 CH4=1500" — the live SERVO_OUTPUT_RAW values
    /// for the first four servo output channels, updated whenever a message
    /// arrives.  Lets the operator see in one glance whether the flight
    /// controller is driving an output (CH2/CH4 sweeping) even when the physical
    /// servo does not respond.  Empty when no feedback is being received.
    QString servoOutputsRaw() const { return m_servoOutputsRaw; }

    /// Live verdict of what the flight controller is actually doing during a
    /// sweep, computed from SERVO_OUTPUT_RAW: the observed PWM swing range of the
    /// primary (CH1) and secondary (CH2) elevon channels plus whether they move
    /// in OPPOSITE or the SAME direction.  Example for a working aileron sweep:
    /// "CH1 800→2200  CH2 2200→800  ✓ OPPOSITE".  Empty outside a sweep or when
    /// no feedback arrives.
    QString sweepVerdict() const { return m_sweepVerdict; }

    Q_INVOKABLE void loadSurfacesForVehicle(const QString &vehicleType, int motorCount);
    Q_INVOKABLE void testSurface(const QString &surfaceId);
    Q_INVOKABLE void testAileron();
    Q_INVOKABLE void testElevator();
    Q_INVOKABLE void testRudder();
    Q_INVOKABLE void testNoseWheel();
    Q_INVOKABLE void testElevonLeft();
    Q_INVOKABLE void testElevonRight();
    Q_INVOKABLE void testFlap();
    Q_INVOKABLE void confirmSurfaceDirection(const QString &surfaceId, bool correct);
    Q_INVOKABLE void stopAllSurfaces();

signals:
    void surfacesChanged();
    void activeSurfaceChanged();
    void surfaceStatesChanged();
    void isArmedChanged();
    void lastErrorMessageChanged();
    void flightModeChanged();
    void sweptPwmChanged();
    void servoOutputsRawChanged();
    void sweepVerdictChanged();
    void swapElevonLRChanged();

private slots:
    void _advanceSweepStep();
    void _forceManualMode();
    void _onCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode);
    void _onMavlinkMessage(const mavlink_message_t &message);
    void _onParametersReady(bool ready);

private:
    void _startSurfaceSweep(int idx);
    void _beginSurfaceTest(int idx);
    void _setElevonGain(bool boost);

    // Sweep-value helpers. RC-driven surfaces always sweep RC neutral (1500 µs)
    // to RC extremes; flaps (DO_SET_SERVO) sweep their SERVOn limits.
    int _sweepCenter(const ControlSurface &surf) const;
    int _sweepMin(const ControlSurface &surf) const;
    int _sweepMax(const ControlSurface &surf) const;

    // Dedicated per-surface send driver. Each test routes its sweep PWM through
    // _sendActivePwm → _sendServoPwm, which applies the correct drive per the
    // surface's rcChannel / rcChannel2 / elevonMode:
    //   - elevon isolation (elevonMode 1/2) → RCMAP_ROLL + RCMAP_PITCH cancel pair
    //   - pure single-axis (aileron/elevator/rudder/nose_wheel) → the axis RCMAP override
    //   - "flap" → MAV_CMD_DO_SET_SERVO fallback (RC override if FLAP_IN_CHANNEL set)
    // _sendCenterRcOverrides pins roll/pitch/yaw to neutral before a new command
    // is applied so stale overrides cannot leak into the elevon mixer.
    void _sendActivePwm(int pwmUs);
    void _sendServoPwm(const ControlSurface &surf, int pwmUs);
    void _sendCenterRcOverrides();
    void _sendFlapPwm(int pwmUs);
    void _sendServoDirectMulti(const QVector<QPair<int,int>> &chPwms); // direct DO_SET_SERVO on multiple channels
    void _sendRcOverride(int rcChannel, int pwmUs);
    void _sendRcOverrideMulti(const QVector<QPair<int, int>> &overrides);
    void _clearRcOverrides();
    void _setServoStreaming(bool enable);
    void _updateSweepVerdict();
    int _findSurfaceIndex(const QString &surfaceId) const;
    int _rcMapChannel(const QString &apName, const QString &px4Name, int fallback) const;
    int _rcMapForSurface(const QString &id) const;
    int _readServoParam(const QString &prefix, const QString &suffix, int fallback) const;

    Vehicle *m_vehicle = nullptr;
    QVector<ControlSurface> m_surfaceList;
    QVector<SurfaceState> m_states;
    int m_activeSurface = -1;
    int m_pendingSurfaceIndex = -1;   /// Surface waiting on an arm ACK before its sweep starts
    bool m_isArmed = false;
    QString m_lastErrorMessage;
    QString m_lastVehicleType;   /// Last loadSurfacesForVehicle() type, used to re-load on parameters-ready
    int m_lastMotorCount = 0;

    QTimer m_stepTimer;
    QTimer m_streamTimer;
    QTimer m_forceManualTimer;
    int m_sweepStep = 0;
    int m_currentStreamPwm = 1500;
    int m_sweepMinPwmActual = 1500;
    int m_sweepMaxPwmActual = 1500;
    int m_sweepMinPwmActual2 = 1500;   /// Observed min PWM on channel2 during the sweep
    int m_sweepMaxPwmActual2 = 1500;   /// Observed max PWM on channel2 during the sweep
    int m_phaseOppositeCount = 0;      /// Samples where CH1 and CH2 moved in opposite directions
    int m_phaseSameCount = 0;          /// Samples where CH1 and CH2 moved in the same direction
    int m_sweptPwm = 0;
    int m_sweptPwm2 = 0;
    QString m_servoOutputsRaw;
    QString m_sweepVerdict;
    QString m_flightMode;
    bool m_swapElevonLR = false;
    double m_savedMixingGain = 0.0;    ///< Original MIXING_GAIN value saved during a sweep (0.0 = none pending)
};
