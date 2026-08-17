/**
 * @file HardwareTestController.h
 * @brief QML-exposed controller for preflight motor and servo hardware tests.
 *
 * Manages two testing paths:
 *   1. Individual motor tests — all vehicles use MAV_CMD_DO_MOTOR_TEST.  For
 *      multirotors the MAVLink motor instance equals the board output; for
 *      fixed-wing the output channel is auto-detected from the SERVOn_FUNCTION
 *      parameters (Throttle/Motor) so the correct output is spun without
 *      requiring RC input.
 *   2. Profile-based servo sweep tests — a sequence of DO_SET_SERVO commands
 *      driven by a HardwareTestProfile loaded from a JSON template.
 *
 * After each motor test the controller runs a configurable cooldown period
 * (kCooldownMs) before the motor is marked available again.  Feedback from
 * SERVO_OUTPUT_RAW is compared against the expected PWM to determine pass/fail.
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
    Q_PROPERTY(bool isFixedWing READ isFixedWing NOTIFY vehicleTypeLabelChanged)
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
    static constexpr int kDefaultDurationSec = 5;  /// Default test hold time in seconds
    static constexpr int kParamTimeoutMs = 10000;  /// Fallback timeout if parameters never report ready
    static constexpr int kResultGraceMs = 1500;    /// Extra ms after test duration before evaluating SERVO_OUTPUT_RAW
    static constexpr int kFwMinPwmUs    = 1000;    /// Fixed-wing throttle output PWM floor
    static constexpr int kFwMaxPwmUs    = 1800;    /// Fixed-wing throttle output PWM ceiling for spin tests
    static constexpr int kFwDefaultPwmUs = 1500;   /// Default fixed-wing test PWM — SERVO3 ≈1500 µs, well above ESC start threshold
    static constexpr int kMultiMaxPwmUs = 1200;    /// Multirotor gentle-spin ceiling

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

    /// True for fixed-wing / flying-wing airframes (planes only have one motor,
    /// tested via the arm → RC override → disarm sequence).
    bool isFixedWing() const { return _isFixedWing(); }

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

    /// Set the raw PWM value (1000–1200 µs) for the given motor; used by motor tests.
    Q_INVOKABLE void setMotorPwm(int motorIndex, int pwmUs);

    /// Returns the fixed-wing motor label (e.g. "M3") based on the auto-detected
    /// servo output channel, or "M1" for multirotors.
    Q_INVOKABLE QString fixedWingTestLabel() const;

    /// Returns the actual servo output channel that will be driven for a given
    /// motor card (1-based).  For multirotors this equals the motor index; for
    /// fixed-wing it returns the auto-detected motor output channel.
    int motorOutputChannel(int motorIndex) const;

    /// Bind this controller to a vehicle and resolve motor count from parameters.
    void setVehicle(Vehicle *vehicle);

    /// Determine motor count from vehicle parameters (PX4 CA_AIRFRAME / ArduPilot FRAME_CLASS)
    /// or fall back to HEARTBEAT type.  Also detects VTOL mode.
    void resolveMotorCount();

    // ── Individual motor test (QML-invokable) ───────────────────────────

    /// Start a test on the given motor (1-based).  All vehicles send
    /// MAV_CMD_DO_MOTOR_TEST; fixed-wing spins the auto-detected output channel.
    Q_INVOKABLE void testMotor(int motorIndex);

    /// Emergency stop: sends zero throttle to all motors and resets timers.
    Q_INVOKABLE void stopAll();

    /// Sends a normal MAVLink disarm command (no force flag).
    /// Use this to recover when the vehicle is left armed after a failed test.
    Q_INVOKABLE void disarmVehicle();


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

    /// Emitted when a servo sweep / profile sequence finishes, with the aggregate result.
    void servoSweepFinished(bool passed, const QString &message);

private slots:
    /// Handles MAV_CMD acknowledgment results for DO_MOTOR_TEST and DO_SET_SERVO.
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

    /// Fixed-wing state machine: re-sends the throttle RC override while the motor spins.
    void _fwOverrideTick();

    /// Fixed-wing state machine: advances arm → spin → spin-down → disarm → evaluate.
    void _fwAdvancePhase();

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

    /// Returns the RC channel used for throttle (from RCMAP_THROTTLE param, default 3).
    /// Used only as a fallback when no SERVOn_FUNCTION mapping is found.
    int _fwThrottleChannel() const;

    /// Auto-detects the servo output channel that drives the fixed-wing motor by
    /// scanning SERVO1..16_FUNCTION for the Throttle (70) or Motor (73) function.
    /// Falls back to the RC throttle channel, then channel 3.  Caches the result.
    int _fwDetectMotorChannel();

    /// Resets the cached fixed-wing motor channel so it is re-detected on the
    /// next access (e.g. after parameters become available or vehicle change).
    void _invalidateMotorChannel() { _cachedFwMotorChannel = -1; }

    /// True when the primary link is a simulator (Mock Link / UDP), where a
    /// flight-mode switch or servo feedback may not behave like real hardware.
    bool _isMockLink() const;

    /// True for fixed-wing / flying-wing airframes.  ArduPilot Plane does NOT
    /// implement MAV_CMD_DO_MOTOR_TEST (it is QuadPlane/Copter/Sub/Rover only),
    /// so these airframes use the arm → RC override → disarm sequence instead.
    bool _isFixedWing() const;

    /// Starts the fixed-wing motor test: arms the vehicle, drives the throttle
    /// channel via RC_CHANNELS_OVERRIDE for the test duration, then idles the
    /// throttle, disarms, and evaluates SERVO_OUTPUT_RAW feedback.
    void _startFixedWingMotorTest(int idx);

    /// Sends RC_CHANNELS_OVERRIDE with the given PWM on the detected motor
    /// (throttle) channel.  ArduPilot applies this as if the RC throttle stick
    /// were held at the requested value.
    void _fwSendOverride(int pwm);

    /// Converts a desired SERVO output PWM (e.g. what SERVO3 should output) into
    /// the equivalent RC input override PWM using the SERVO{ch} and RC{ch}
    /// calibration parameters.  Falls back to a near-identity mapping when the
    /// parameters are not yet available.
    int _fwPwmToOverride(int servoPwmUs);

    /// Terminates the fixed-wing motor test, sets the motor result, and returns
    /// the controller to idle.  Also disarms if the vehicle is still armed.
    void _fwFinishTest(bool passed, const QString &result);

    /// Core of testMotor() after the armed check: resolves the fixed-wing MANUAL
    /// mode gate and starts the appropriate test sequence.  Also called from
    /// _onCommandResult() once a deferred disarm is ACKed.
    void _startMotorTestInternal(int motorIndex);

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

    // ── Fixed-wing motor test state ─────────────────────────────────────

    /// Cached fixed-wing motor output channel (1-based), -1 until detected by
    /// _fwDetectMotorChannel() so params aren't re-scanned on every access.
    int _cachedFwMotorChannel = -1;

    /// True once the last MAV_CMD_DO_MOTOR_TEST was ACKed (not rejected).  Used
    /// to treat feedback-less environments (Mock Link, SITL) as PASS instead of
    /// TIMEOUT when SERVO_OUTPUT_RAW is never streamed.
    bool _lastCommandAcked = false;

    /// Motor test requested while the vehicle was armed; the real test starts in
    /// _onCommandResult() once the disarm command is ACKed (-1 = none pending).
    int _pendingMotorIndex = -1;

    /// When true, skip the fixed-wing MANUAL-mode gate.  Intended as an advanced
    /// user toggle for simulation / bench testing where the mode is not swittable.
    bool _fwAllowNonManualTest = false;

    /// Re-sends RC_CHANNELS_OVERRIDE every second while the FW motor spins
    /// (ArduPilot drops the override after RC_OVERRIDE_TIME without refresh).
    QTimer _fwOverrideTimer;
    /// Single-shot timer advancing the FW phase machine (arm-wait, spin, spin-down, disarm).
    QTimer _fwPhaseTimer;
    /// 0-based motor index currently under the FW test, or -1 when idle.
    int _fwTestIndex = -1;
    /// Phase: 0=idle, 1=waiting-for-arm, 2=spinning, 3=spin-down, 4=waiting-for-disarm.
    int _fwPhase = 0;
    /// PWM commanded during the FW spin phase.
    int _fwTargetPwm = 0;
    /// Number of arm attempts so far (each followed by a 500 ms wait).
    int _fwArmTries = 0;

    static constexpr int kFwArmRetries = 6;    /// ~3 s total arm wait before giving up
    static constexpr int kFwSpinDownMs = 800;  /// Time to let the motor stop before disarming
};
