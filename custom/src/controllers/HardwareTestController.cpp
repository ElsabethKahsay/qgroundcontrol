#include "HardwareTestController.h"

#include <QDebug>
#include <QLoggingCategory>

#include "Vehicle/Vehicle.h"

Q_LOGGING_CATEGORY(hardwareTestLog, "preflight.hardwaretest")

HardwareTestController::HardwareTestController(QObject *parent)
    : QObject(parent)
{
    _stepTimer.setSingleShot(true);
    connect(&_stepTimer, &QTimer::timeout, this, &HardwareTestController::_advanceStep);
}

void HardwareTestController::setVehicle(Vehicle *vehicle)
{
    if (_vehicle) {
        disconnect(_vehicle, &Vehicle::mavCommandResult, this, &HardwareTestController::_onCommandResult);
        disconnect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &HardwareTestController::_onMavlinkMessage);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        connect(_vehicle, &Vehicle::mavCommandResult, this, &HardwareTestController::_onCommandResult);
        connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &HardwareTestController::_onMavlinkMessage);
    }
}

QVariant HardwareTestController::sequenceCompleted() const
{
    return _sequenceCompleted ? QVariant(true) : QVariant();
}

void HardwareTestController::runMotorTest()
{
    if (_running || !_vehicle) {
        qCDebug(hardwareTestLog) << "Cannot start motor test: running =" << _running << "vehicle =" << (_vehicle != nullptr);
        return;
    }

    _isMotorTest = true;
    _profileSteps.clear();
    for (int i = 0; i < 4; ++i) {
        TestStep step;
        step.name = QStringLiteral("Motor %1").arg(i + 1);
        step.testType = QStringLiteral("motor");
        step.motorInstance = i + 1;
        step.targetPwm = 1500;
        step.throttlePct = 35;
        step.expectedMin = 800;
        step.expectedMax = 2200;
        step.durationMs = 3000;
        step.settleMs = 1000;
        step.needsVisualConfirm = false;
        _profileSteps.append(step);
    }
    _startTest();
}

void HardwareTestController::runServoSweep()
{
    if (_running || !_vehicle) {
        qCDebug(hardwareTestLog) << "Cannot start servo sweep: running =" << _running << "vehicle =" << (_vehicle != nullptr);
        return;
    }

    _isMotorTest = false;
    _profileSteps.clear();

    QVector<int> pwmValues = {1200, 1500, 1800, 1500, 1200};
    for (int servo = 1; servo <= 4; ++servo) {
        for (int pwm : pwmValues) {
            TestStep step;
            step.name = QStringLiteral("Servo %1 PWM %2").arg(servo).arg(pwm);
            step.testType = QStringLiteral("servo");
            step.servoInstance = servo;
            step.targetPwm = pwm;
            step.expectedMin = pwm - kPwmTolerance;
            step.expectedMax = pwm + kPwmTolerance;
            step.durationMs = 800;
            step.settleMs = 200;
            step.needsVisualConfirm = false;
            _profileSteps.append(step);
        }
    }
    _startTest();
}

bool HardwareTestController::runProfile(const QString &filePath)
{
    if (_running) {
        qCDebug(hardwareTestLog) << "Cannot load profile while test is running";
        return false;
    }

    QString error;
    HardwareTestProfile profile = HardwareTestProfile::loadFromJsonFile(filePath, &error);
    if (!profile.isValid()) {
        _lastError = QStringLiteral("Failed to load profile: %1").arg(error);
        emit lastErrorMessageChanged();
        qCDebug(hardwareTestLog) << _lastError;
        return false;
    }

    _profileName = profile.profileName;
    _profileSteps = profile.steps;
    _isMotorTest = false;

    // Auto-detect if this is a motor-only profile
    bool allMotor = true;
    for (const TestStep &step : _profileSteps) {
        if (step.testType != QStringLiteral("motor")) {
            allMotor = false;
            break;
        }
    }
    _isMotorTest = allMotor;

    qCDebug(hardwareTestLog) << "Loaded profile:" << _profileName
                             << "with" << _profileSteps.size() << "steps";
    _startTest();
    return true;
}

void HardwareTestController::abortSequence()
{
    if (!_running) return;

    _stepTimer.stop();
    _finishTest(false, QStringLiteral("Test aborted by user"));
}

void HardwareTestController::_startTest()
{
    // Reset feedback buffer
    for (int i = 0; i < kServoCount; ++i)
        _feedbackPwm[i] = 0;

    _running = true;
    _allPassed = true;
    _sequenceCompleted = false;
    _currentStep = 0;
    _lastError.clear();
    _progress = 0.0;
    _totalSteps = _profileSteps.size();

    emit isRunningChanged();
    emit allPassedChanged();
    emit sequenceCompletedChanged();
    emit lastErrorMessageChanged();
    emit stepProgressChanged();

    qCDebug(hardwareTestLog) << "Starting test with" << _totalSteps << "steps";
    _advanceStep();
}

void HardwareTestController::_advanceStep()
{
    if (_currentStep >= _totalSteps) {
        _finishTest(_allPassed, QString());
        return;
    }

    const TestStep &step = _profileSteps[_currentStep];

    // Before sending the next command, verify feedback from the previous step
    if (_currentStep > 0) {
        const TestStep &prev = _profileSteps[_currentStep - 1];
        bool feedbackOk = false;
        if (prev.testType == QStringLiteral("motor")) {
            feedbackOk = _verifyMotorFeedback(prev.motorInstance);
        } else {
            if (prev.expectedMin > 0 && prev.expectedMax > 0) {
                feedbackOk = _verifyServoFeedbackRange(prev.servoInstance, prev.expectedMin, prev.expectedMax);
            } else {
                feedbackOk = _verifyServoFeedback(prev.servoInstance, prev.targetPwm);
            }
        }
        if (!feedbackOk) {
            _allPassed = false;
            QString err = prev.testType == QStringLiteral("motor")
                ? QStringLiteral("Motor %1: feedback mismatch").arg(prev.motorInstance)
                : QStringLiteral("Servo %1: expected range %2\u2013%3 \u00b5s, got %4 \u00b5s")
                      .arg(prev.servoInstance)
                      .arg(prev.expectedMin).arg(prev.expectedMax)
                      .arg(_feedbackPwm[prev.servoInstance - 1]);
            _lastError = err;
            emit lastErrorMessageChanged();
            qCDebug(hardwareTestLog) << err;
        } else {
            qCDebug(hardwareTestLog).noquote()
                << (prev.testType == QStringLiteral("motor") ? "Motor" : "Servo")
                << (prev.testType == QStringLiteral("motor") ? prev.motorInstance : prev.servoInstance)
                << "feedback OK (" << _feedbackPwm[prev.testType == QStringLiteral("motor") ? prev.motorInstance - 1 : prev.servoInstance - 1] << ")";
        }
    }

    _progress = static_cast<qreal>(_currentStep) / _totalSteps;
    emit stepProgressChanged();

    if (step.testType == QStringLiteral("motor")) {
        int commandValue = step.throttlePct >= 0 ? step.throttlePct : step.targetPwm;
        _sendMotorStep(step.motorInstance, commandValue, step.durationMs / 1000);
    } else {
        _sendServoStep(step.servoInstance, step.targetPwm, step.durationMs / 1000);
    }

    _stepTimer.start(step.durationMs + step.settleMs);
    _currentStep++;
}

void HardwareTestController::_sendMotorStep(int motorInstance, int throttlePct, int durationSec)
{
    if (!_vehicle) return;
    qCDebug(hardwareTestLog) << "Motor test: instance" << motorInstance
                             << "throttle" << throttlePct << "% duration" << durationSec << "s";
    _vehicle->motorTest(motorInstance, throttlePct, durationSec, false);
}

void HardwareTestController::_sendServoStep(int servoInstance, int pwmValue, int durationSec)
{
    if (!_vehicle) return;
    qCDebug(hardwareTestLog) << "Servo sweep: servo" << servoInstance
                             << "PWM" << pwmValue << "duration" << durationSec << "s";
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_SET_SERVO,
        false,
        servoInstance,
        pwmValue
    );
}

bool HardwareTestController::_verifyServoFeedback(int servoInstance, int expectedPwm) const
{
    if (servoInstance < 1 || servoInstance > kServoCount)
        return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0)
        return false;
    int delta = qAbs(static_cast<int>(actual) - expectedPwm);
    return delta <= kPwmTolerance;
}

bool HardwareTestController::_verifyServoFeedbackRange(int servoInstance, int expectedMin, int expectedMax) const
{
    if (servoInstance < 1 || servoInstance > kServoCount)
        return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0)
        return false;
    return actual >= static_cast<uint16_t>(expectedMin) && actual <= static_cast<uint16_t>(expectedMax);
}

bool HardwareTestController::_verifyMotorFeedback(int motorInstance) const
{
    if (motorInstance < 1 || motorInstance > kServoCount)
        return false;
    uint16_t actual = _feedbackPwm[motorInstance - 1];
    if (actual == 0)
        return false;
    return actual > 800 && actual < 2200;
}

void HardwareTestController::_finishTest(bool passed, const QString &error)
{
    _running = false;
    _allPassed = passed;
    _sequenceCompleted = true;
    _lastError = error;
    _progress = 1.0;

    emit isRunningChanged();
    emit allPassedChanged();
    emit sequenceCompletedChanged();
    emit lastErrorMessageChanged();
    emit stepProgressChanged();

    if (error.isEmpty()) {
        qCDebug(hardwareTestLog) << "Test finished:" << (passed ? "PASSED" : "FAILED");
    } else {
        qCDebug(hardwareTestLog) << "Test finished:" << (passed ? "PASSED" : "FAILED") << "-" << error;
    }
}

void HardwareTestController::_onCommandResult(int cmdId, int compId, int mavResult)
{
    Q_UNUSED(cmdId)
    Q_UNUSED(compId)
    if (mavResult != MAV_RESULT_ACCEPTED && _running) {
        _allPassed = false;
        _lastError = QStringLiteral("Command rejected (result %1)").arg(mavResult);
        emit allPassedChanged();
        emit lastErrorMessageChanged();
    }
}

void HardwareTestController::_onMavlinkMessage(const mavlink_message_t &message)
{
    if (message.msgid != MAVLINK_MSG_ID_SERVO_OUTPUT_RAW)
        return;

    mavlink_servo_output_raw_t servo;
    mavlink_msg_servo_output_raw_decode(&message, &servo);

    if (servo.port != 0)
        return;

    _feedbackPwm[0]  = servo.servo1_raw;
    _feedbackPwm[1]  = servo.servo2_raw;
    _feedbackPwm[2]  = servo.servo3_raw;
    _feedbackPwm[3]  = servo.servo4_raw;
    _feedbackPwm[4]  = servo.servo5_raw;
    _feedbackPwm[5]  = servo.servo6_raw;
    _feedbackPwm[6]  = servo.servo7_raw;
    _feedbackPwm[7]  = servo.servo8_raw;
    _feedbackPwm[8]  = servo.servo9_raw;
    _feedbackPwm[9]  = servo.servo10_raw;
    _feedbackPwm[10] = servo.servo11_raw;
    _feedbackPwm[11] = servo.servo12_raw;
    _feedbackPwm[12] = servo.servo13_raw;
    _feedbackPwm[13] = servo.servo14_raw;
    _feedbackPwm[14] = servo.servo15_raw;
    _feedbackPwm[15] = servo.servo16_raw;
}
