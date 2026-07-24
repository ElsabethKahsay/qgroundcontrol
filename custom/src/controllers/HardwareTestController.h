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
    enum MotorState { Idle = 0, Testing = 1, Pass = 2, Fail = 3, Cooldown = 4 };
    Q_ENUM(MotorState)

    static constexpr int kPwmTolerance = 50;
    static constexpr int kServoCount = 16;
    static constexpr int kCooldownMs = 2000;
    static constexpr int kMaxThrottlePct = 25;
    static constexpr int kDefaultThrottlePct = 5;
    static constexpr int kDefaultDurationSec = 3;
    static constexpr int kParamTimeoutMs = 10000;
    static constexpr int kResultGraceMs = 1500;  // extra time after test duration before evaluating result

    explicit HardwareTestController(QObject *parent = nullptr);

    int motorCount() const { return _motorCount; }
    QVariantList motorStates() const;
    int activeMotor() const { return _activeMotor; }
    bool isArmed() const { return _isArmed; }
    bool isVtol() const { return _vtolMode; }
    int hoverMotorCount() const { return _hoverMotorCount; }
    bool isCooldownActive() const { return _cooldownIndex >= 0; }
    int cooldownMsRemaining() const { return _cooldownRemaining * 250; }
    QString vehicleTypeLabel() const { return _vehicleTypeLabel; }
    QString vehicleAutopilotLabel() const { return _vehicleAutopilotLabel; }
    bool firstTestDone() const { return _firstTestDone; }
    void setFirstTestDone(bool done);
    int durationSec() const { return _durationSec; }
    void setDurationSec(int sec);
    QVariantList motorPwmValues() const;

    // Per-motor accessors
    Q_INVOKABLE QString motorPosition(int motorIndex) const;
    Q_INVOKABLE int motorStatus(int motorIndex) const;
    Q_INVOKABLE int motorFeedback(int motorIndex) const;
    Q_INVOKABLE double targetThrottle(int motorIndex) const;
    Q_INVOKABLE int motorDuration(int motorIndex) const;
    Q_INVOKABLE void setTargetThrottle(int motorIndex, double pct);
    Q_INVOKABLE void setMotorDuration(int motorIndex, int sec);
    Q_INVOKABLE void setMotorPwm(int motorIndex, int pwmUs);

    void setVehicle(Vehicle *vehicle);
    void resolveMotorCount();

    Q_INVOKABLE void testMotor(int motorIndex);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void resetMotorState(int motorIndex);
    Q_INVOKABLE void resetAll();

    // Legacy — keep for profile/servo tests
    Q_INVOKABLE void runServoSweep();
    Q_INVOKABLE bool runProfile(const QString &filePath);
    Q_INVOKABLE void abortSequence();
    bool isRunning() const { return _running; }
    bool allPassed() const { return _allPassed; }
    QVariant sequenceCompleted() const;
    QString lastErrorMessage() const { return _lastError; }
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
    void _onCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode);
    void _onMavlinkMessage(const mavlink_message_t &message);
    void _onArmedChanged();
    void _onParametersReady();
    void _onParamTimeout();
    void _evaluateTestResult();
    void _cooldownTick();
    void _advanceStep();

private:
    void _setMotorState(int index, MotorState state);
    void _updateMotorCount(int count, bool hasHover = false, int hoverCount = 0);
    QString _vehicleTypeLabelFromSource(const QString &source) const;
    void _logTestResult(int motorIndex, int throttlePct, int durationSec,
                        int expectedPwm, int actualPwm, int pwmDelta,
                        const QString &result);
    void _sendStopToMotor(int index);
    void _setServoStreaming(bool enable);
    void _startTest();
    void _finishTest(bool passed, const QString &error);
    void _sendMotorStep(int motorInstance, int throttlePct, int durationSec);
    void _sendServoStep(int servoInstance, int pwmValue, int durationSec);
    bool _verifyServoFeedback(int servoInstance, int expectedPwm) const;
    bool _verifyServoFeedbackRange(int servoInstance, int expectedMin, int expectedMax) const;
    bool _verifyMotorFeedback(int motorInstance) const;
    static QString _positionForMotor(int motorIndex, int totalCount);

    // Fixed-wing motor test: arm → RC_CHANNELS_OVERRIDE → disarm
    void _fwStartTest(int motorIndex, int pwmUs);
    void _fwSendRcOverride(uint16_t throttlePwm);
    void _fwDisarm();
    int  _fwThrottleChannel() const;

    Vehicle *_vehicle = nullptr;

    int _motorCount = 0;
    QVector<MotorState> _state;
    int _activeMotor = -1;
    bool _isArmed = false;

    // Per-motor settings
    QVector<double> _targetThrottle;
    QVector<int> _motorDuration;
    QVector<int> _motorPwmValues;   // per-motor PWM µs (1000–1200)
    int _durationSec = kDefaultDurationSec; // shared test duration

    uint16_t _feedbackPwm[kServoCount] = {};
    uint16_t _peakFeedbackPwm[kServoCount] = {};
    QTimer _resultTimer;
    QTimer _cooldownTimer;
    int _cooldownIndex = -1;
    int _cooldownRemaining = 0;
    int _expectedPwm = 0;
    MotorState _cooldownTargetState = Idle;

    // Vehicle type labels
    QString _vehicleTypeLabel = QStringLiteral("Unknown Device");
    QString _vehicleAutopilotLabel;

    // VTOL
    bool _vtolMode = false;
    int _hoverMotorCount = 0;

    QTimer _paramTimeoutTimer;
    bool _paramTimeoutFired = false;

    bool _firstTestDone = false;

    // Legacy profile test members
    bool _running = false;
    bool _allPassed = false;
    bool _sequenceCompleted = false;
    int _currentStep = 0;
    int _totalSteps = 0;
    qreal _progress = 0.0;
    QString _lastError;
    bool _isMotorTest = false;
    QString _profileName;
    QTimer _stepTimer;
    QVector<TestStep> _profileSteps;

    // Fixed-wing motor test state machine
    enum FwTestPhase { FwIdle, FwArming, FwSpinning, FwStopping, FwDisarming };
    FwTestPhase _fwPhase = FwIdle;
    int         _fwMotorIndex = -1;
    int         _fwPwmUs = 1150;
    QTimer      _fwTestTimer;     // fires after test duration → stop throttle
    QTimer      _fwDisarmTimer;   // fires after stop → disarm
};
