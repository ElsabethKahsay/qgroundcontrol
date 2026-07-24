/**
 * @file HardwareTestController.h
 * @brief QML-exposed controller for preflight motor and servo hardware tests.
 *
 * Manages two testing paths:
 *   1. Individual motor tests — copters use MAV_CMD_DO_MOTOR_TEST; fixed-wing
 *      aircraft arm, then drive the throttle channel via RC_CHANNELS_OVERRIDE.
 *   2. Profile-based servo sweep tests — a sequence of DO_SET_SERVO commands
 *      driven by a HardwareTestProfile loaded from a JSON template.
 *
 * After each motor test the controller runs a configurable cooldown period
 * (kCooldownMs) before the motor is marked available again.  Feedback from
 * SERVO_OUTPUT_RAW is compared against the expected PWM to determine pass/fail.
 *
 * Fixed-wing motor test state machine:
 *   FwIdle → FwArming → FwSpinning → FwStopping → FwDisarming → FwIdle
 *   - FwIdle:      no test running, ready to accept a new request.
 *   - FwArming:    ARM command sent, waiting for ACK.
 *   - FwSpinning:  armed; throttle channel held at target PWM via RC override.
 *   - FwStopping:  test duration elapsed; throttle set to disarmed (1000 µs).
 *   - FwDisarming: DISARM command sent, waiting for ACK before returning to idle.
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>

#include "QGCMAVLink.h"
#include "HardwareTestProfile.h"

class Vehicle;

/**
 * @class HardwareTestController
 * @brief Exposes motor/servo preflight testing to QML.
 *
 * Properties drive the test UI (motor states, active motor, cooldown timer,
 * PWM values, vehicle labels).  Invokable methods let QML start/stop tests,
 * adjust per-motor throttle and duration, and load profile files.
 */
class HardwareTestController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int motorCount READ motorCount NOTIFY motorCountChanged)
    Q_PROPERTY(QVariantList motorStates READ motorStates NOTIFY motorStatesChanged)
    Q_PROPERTY(int activeMotor READ activeMotor NOTIFY activeMotorChanged)
    Q_PROPERTY(bool isArmed READ isArmed NOTIFY isArmedChanged)
    Q_PROPERTY(bool isVtol READ isVtol NOTIFY vtolChanged)
    Q_PROPERTY(int hoverMotorCount READ hoverMotorCount NOTIFY vtolChanged)
    Q_PROPERTY(bool cooldownActive READ isCooldownActive NOTIFY motorStatesChanged)
    Q_PROPERTY(int cooldownMsRemaining READ cooldownMsRemaining NOTIFY motorStatesChanged)
    Q_PROPERTY(QString vehicleTypeLabel READ vehicleTypeLabel NOTIFY vehicleTypeLabelChanged)
    Q_PROPERTY(QString vehicleAutopilotLabel READ vehicleAutopilotLabel NOTIFY vehicleTypeLabelChanged)
    Q_PROPERTY(bool firstTestDone READ firstTestDone WRITE setFirstTestDone NOTIFY firstTestDoneChanged)
    Q_PROPERTY(int durationSec READ durationSec WRITE setDurationSec NOTIFY durationSecChanged)
    Q_PROPERTY(QVariantList motorPwmValues READ motorPwmValues NOTIFY motorPwmValuesChanged)

public:
    /// States a single motor can be in during testing.
    enum MotorState { Idle = 0, Testing = 1, Pass = 2, Fail = 3, Cooldown = 4 };
    Q_ENUM(MotorState)

    static constexpr int kPwmTolerance = 50;       /// Allowed µs deviation between commanded and feedback PWM
    static constexpr int kServoCount = 16;         /// Maximum number of servo/motor outputs tracked
    static constexpr int kCooldownMs = 2000;       /// Post-test cooldown before the motor is marked Idle again
    static constexpr int kMaxThrottlePct = 25;     /// Hard ceiling on throttle % for copter motor tests
    static constexpr int kDefaultThrottlePct = 5;  /// Initial throttle % shown in the UI
    static constexpr int kDefaultDurationSec = 3;  /// Default test hold time in seconds
    static constexpr int kParamTimeoutMs = 10000;  /// Fallback timeout if parameters never report ready
    static constexpr int kResultGraceMs = 1500;    /// Extra ms after test duration before evaluating SERVO_OUTPUT_RAW

    explicit HardwareTestController(QObject *parent = nullptr);

    int motorCount() const { return _motorCount; }
    QVariantList motorStates() const;
    int activeMotor() const { return _activeMotor; }
    bool isArmed() const { return _isArmed; }
    bool isVtol() const { return _vtolMode; }
    int hoverMotorCount() const { return _hoverMotorCount; }

    /// True when a motor is in its post-test cooldown window.
    bool isCooldownActive() const { return _cooldownIndex >= 0; }

    /// Milliseconds remaining in the current cooldown (250 ms tick granularity).
    int cooldownMsRemaining() const { return _cooldownRemaining * 250; }

    QString vehicleTypeLabel() const { return _vehicleTypeLabel; }
    QString vehicleAutopilotLabel() const { return _vehicleAutopilotLabel; }

    /// True after at least one motor test has completed (used to show the results UI).
    bool firstTestDone() const { return _firstTestDone; }
    void setFirstTestDone(bool done);

    /// Shared test duration in seconds applied to all motors unless overridden.
    int durationSec() const { return _durationSec; }
    void setDurationSec(int sec);

    QVariantList motorPwmValues() const;

    // ── Per-motor accessors ─────────────────────────────────────────────

    /// Human-readable position label for the given motor (1-based index).
    Q_INVOKABLE QString motorPosition(int motorIndex) const;

    /// Current MotorState enum value for the given motor (1-based index).
    Q_INVOKABLE int motorStatus(int motorIndex) const;

    /// Latest PWM feedback value (µs) received from SERVO_OUTPUT_RAW for this motor output.
    Q_INVOKABLE int motorFeedback(int motorIndex) const;

    /// User-configured throttle percentage for the given motor (1-based index).
    Q_INVOKABLE double targetThrottle(int motorIndex) const;

    /// User-configured test duration (seconds) for the given motor (1-based index).
    Q_INVOKABLE int motorDuration(int motorIndex) const;

    Q_INVOKABLE void setTargetThrottle(int motorIndex, double pct);
    Q_INVOKABLE void setMotorDuration(int motorIndex, int sec);

    /// Set the raw PWM value (1000–1200 µs) for the given motor; used by fixed-wing tests.
    Q_INVOKABLE void setMotorPwm(int motorIndex, int pwmUs);

    /// Bind this controller to a vehicle and resolve motor count from parameters.
    void setVehicle(Vehicle *vehicle);

    /// Determine motor count from vehicle parameters (PX4 CA_AIRFRAME / ArduPilot FRAME_CLASS)
    /// or fall back to HEARTBEAT type.  Also detects VTOL mode.
    void resolveMotorCount();

    // ── Individual motor test (QML-invokable) ───────────────────────────

    /// Start a test on the given motor (1-based).  Copters send DO_MOTOR_TEST;
    /// fixed-wings go through the FwIdle → … → FwIdle state machine.
    Q_INVOKABLE void testMotor(int motorIndex);

    /// Emergency stop: sends zero throttle to all motors, disarms fixed-wing, resets timers.
    Q_INVOKABLE void stopAll();

    /// Reset a single motor's state back to Idle (does not send any commands).
    Q_INVOKABLE void resetMotorState(int motorIndex);

    /// Reset all motors to Idle and clear all feedback/cooldown data.
    Q_INVOKABLE void resetAll();

    // ── Legacy profile/servo sweep API ──────────────────────────────────

    /// Run a built-in servo sweep across servos 1–4 (1200 → 1500 → 1800 → 1500 → 1200 µs).
    Q_INVOKABLE void runServoSweep();

    /// Load and run a hardware test profile from a JSON file on disk.
    Q_INVOKABLE bool runProfile(const QString &filePath);

    /// Abort the currently running profile/sweep sequence.
    Q_INVOKABLE void abortSequence();

    /// Retrieve hardware test event log entries for the given flight ID.
    Q_INVOKABLE QString getHardwareTestEvents(int flightId);

    bool isRunning() const { return _running; }
    bool allPassed() const { return _allPassed; }

    /// QVariant(true) when the sequence finishes, empty QVariant otherwise.
    QVariant sequenceCompleted() const;
    QString lastErrorMessage() const { return _lastError; }

    /// Progress of the current profile sequence (0.0 – 1.0).
    qreal stepProgress() const { return _progress; }

signals:
    void motorCountChanged();
    void motorStatesChanged();
    void activeMotorChanged();
    void isArmedChanged();
    void vtolChanged();
    void vehicleTypeLabelChanged();
    void firstTestDoneChanged();

    // Per-motor slider updates
    void targetThrottleChanged(int motorIndex, double pct);
    void motorDurationChanged(int motorIndex, int sec);
    void durationSecChanged();
    void motorPwmValuesChanged();

    // Legacy signals
    void isRunningChanged();
    void allPassedChanged();
    void sequenceCompletedChanged();
    void lastErrorMessageChanged();
    void stepProgressChanged();

private slots:
    /// Handles MAV_CMD acknowledgment results for ARM, DO_MOTOR_TEST, and DO_SET_SERVO.
    void _onCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode);

    /// Processes incoming MAVLink messages; extracts SERVO_OUTPUT_RAW for PWM feedback.
    void _onMavlinkMessage(const mavlink_message_t &message);

    /// Syncs _isArmed with the vehicle; auto-stops tests if the vehicle arms externally.
    void _onArmedChanged();

    /// Called when ParameterManager reports parameters ready; resolves motor count.
    void _onParametersReady();

    /// Called if parameters never become ready within kParamTimeoutMs; falls back to HEARTBEAT.
    void _onParamTimeout();

    /// Timer callback: compares peak feedback PWM against expected value to determine pass/fail.
    void _evaluateTestResult();

    /// Decrements the cooldown counter each 250 ms tick; restores motor state when done.
    void _cooldownTick();

    /// Profile sequence: validates the previous step's feedback and sends the next servo/motor command.
    void _advanceStep();

private:
    void _setMotorState(int index, MotorState state);
    void _updateMotorCount(int count, bool hasHover = false, int hoverCount = 0);

    /// Derives a human-readable vehicle type label from the motor-count resolution source string.
    QString _vehicleTypeLabelFromSource(const QString &source) const;

    /// Writes a test result row (motor, throttle, PWM, delta, result) to the database and logs it.
    void _logTestResult(int motorIndex, int throttlePct, int durationSec,
                        int expectedPwm, int actualPwm, int pwmDelta,
                        const QString &result);

    /// Sends DO_MOTOR_TEST with 1000 µs (disarmed) to stop a specific motor.
    void _sendStopToMotor(int index);

    /// Enables or disables SERVO_OUTPUT_RAW streaming at 10 Hz for feedback collection.
    void _setServoStreaming(bool enable);

    /// Initializes all profile-sequence state and emits isRunningChanged().
    void _startTest();

    /// Marks the sequence complete, sets final state, and emits all completion signals.
    void _finishTest(bool passed, const QString &error);

    /// Sends DO_MOTOR_TEST command for a single motor (profile-sequence path).
    void _sendMotorStep(int motorInstance, int throttlePct, int durationSec);

    /// Sends DO_SET_SERVO command for a single servo (profile-sequence path).
    void _sendServoStep(int servoInstance, int pwmValue, int durationSec);

    /// Checks that feedback PWM for a servo is within kPwmTolerance of the expected value.
    bool _verifyServoFeedback(int servoInstance, int expectedPwm) const;

    /// Checks that feedback PWM for a servo falls within [expectedMin, expectedMax].
    bool _verifyServoFeedbackRange(int servoInstance, int expectedMin, int expectedMax) const;

    /// Checks that motor feedback PWM is in the plausible 800–2200 µs range.
    bool _verifyMotorFeedback(int motorInstance) const;

    /// Returns a position string (e.g. "Front Right") for a motor given its index and total count.
    static QString _positionForMotor(int motorIndex, int totalCount);

    // ── Fixed-wing motor test helpers ───────────────────────────────────

    /// Begins the fixed-wing test sequence: arms the vehicle, then drives throttle via RC override.
    void _fwStartTest(int motorIndex, int pwmUs);

    /// Sends RC_CHANNELS_OVERRIDE with the throttle channel set to the given PWM; all other channels neutral.
    void _fwSendRcOverride(uint16_t throttlePwm);

    /// Sends a disarm command (MAV_CMD_COMPONENT_ARM_DISARM with param1=0).
    void _fwDisarm();

    /// Returns the RC channel used for throttle (from RCMAP_THROTTLE param, default 3).

    Vehicle *_vehicle = nullptr;

    int _motorCount = 0;            /// Total number of motor/servo outputs detected from parameters
    QVector<MotorState> _state;     /// Per-motor state array (indexed 0-based; QML uses 1-based)
    int _activeMotor = -1;          /// 1-based index of the motor currently being tested, or -1
    bool _isArmed = false;          /// Cached armed state, kept in sync via _onArmedChanged()

    // Per-motor settings
    QVector<double> _targetThrottle;  /// Throttle percentage (0–kMaxThrottlePct) per motor
    QVector<int> _motorDuration;      /// Per-motor test duration (seconds), overrides _durationSec
    QVector<int> _motorPwmValues;     /// Per-motor raw PWM µs (1000–1200) for fixed-wing tests
    int _durationSec = kDefaultDurationSec; /// Shared test duration used by copter path and profiles

    // ── Feedback and cooldown state ─────────────────────────────────────

    uint16_t _feedbackPwm[kServoCount] = {};      /// Latest SERVO_OUTPUT_RAW values per channel
    uint16_t _peakFeedbackPwm[kServoCount] = {};   /// Highest PWM observed during the active test window
    QTimer _resultTimer;      /// Fires kResultGraceMs after test duration to evaluate feedback
    QTimer _cooldownTimer;    /// 250 ms tick timer driving the cooldown countdown
    int _cooldownIndex = -1;  /// Index of the motor currently in cooldown, or -1 if none
    int _cooldownRemaining = 0; /// Remaining cooldown ticks (each tick = 250 ms)
    int _expectedPwm = 0;     /// The PWM value we commanded for the current test
    MotorState _cooldownTargetState = Idle; /// State to restore after cooldown completes

    // Vehicle type labels
    QString _vehicleTypeLabel = QStringLiteral("Unknown Device");  /// e.g. "Quadcopter", "Fixed Wing"
    QString _vehicleAutopilotLabel;  /// "PX4", "ArduPilot", or empty

    // VTOL
    bool _vtolMode = false;       /// True for VTOL airframes (hover + forward motors)
    int _hoverMotorCount = 0;     /// Number of hover/thrust motors on VTOL frames

    QTimer _paramTimeoutTimer;    /// Safety net: if parameters never load, fall back to HEARTBEAT
    bool _paramTimeoutFired = false; /// True if the param timeout fired (prevents double resolution)

    bool _firstTestDone = false;  /// Latched true after the first test completes (UI transitions)

    // ── Legacy profile-based testing members ────────────────────────────

    bool _running = false;        /// True while a profile/sweep sequence is executing
    bool _allPassed = false;      /// Aggregate result of all completed steps in the sequence
    bool _sequenceCompleted = false; /// Latched true when the sequence finishes (pass or fail)
    int _currentStep = 0;         /// Index of the next step to execute in _profileSteps
    int _totalSteps = 0;          /// Total number of steps (set from _profileSteps.size())
    qreal _progress = 0.0;        /// Fraction of steps completed (0.0 – 1.0)
    QString _lastError;           /// Description of the most recent failure
    bool _isMotorTest = false;    /// True if all steps in the current profile are motor tests
    QString _profileName;         /// Name of the loaded profile (from JSON)
    QTimer _stepTimer;            /// Single-shot timer between profile steps
    QVector<TestStep> _profileSteps; /// The loaded sequence of test steps

    // ── Fixed-wing motor test state machine ─────────────────────────────
    //
    //  FwIdle ──arm cmd──▶ FwArming ──arm ACK──▶ FwSpinning
    //                                                  │  duration elapsed
    //                                                  ▼
    //  FwIdle ◀──disarm ACK── FwDisarming ◀──500ms── FwStopping

    enum FwTestPhase { FwIdle, FwArming, FwSpinning, FwStopping, FwDisarming };
    FwTestPhase _fwPhase = FwIdle;  /// Current state of the fixed-wing test state machine
    int         _fwMotorIndex = -1; /// 1-based motor index for the current FW test
    int         _fwPwmUs = 1150;    /// Target throttle PWM (µs) for the FW test
    QTimer      _fwTestTimer;       /// Fires after _durationSec → sends throttle to 1000 µs (stop)
    QTimer      _fwDisarmTimer;     /// Fires 500 ms after stop → sends disarm command
};
