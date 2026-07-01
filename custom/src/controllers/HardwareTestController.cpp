// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: HardwareTestController.cpp
// Description: Motor test (DO_MOTOR_TEST) and servo sweep (DO_SET_SERVO) controller.
// Used by MotorTestPanel.qml, MotorTestCard.qml, ServoTestCard.qml.

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
    }
    _vehicle = vehicle;
    if (_vehicle) {
        connect(_vehicle, &Vehicle::mavCommandResult, this, &HardwareTestController::_onCommandResult);
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
    _steps.clear();
    _steps.append({1, 35, 3000});
    _steps.append({2, 35, 3000});
    _steps.append({3, 35, 3000});
    _steps.append({4, 35, 3000});
    _startTest();
}

void HardwareTestController::runServoSweep()
{
    if (_running || !_vehicle) {
        qCDebug(hardwareTestLog) << "Cannot start servo sweep: running =" << _running << "vehicle =" << (_vehicle != nullptr);
        return;
    }

    _isMotorTest = false;
    _steps.clear();

    QVector<int> pwmValues = {1200, 1500, 1800, 1500, 1200};
    for (int servo = 1; servo <= 4; ++servo) {
        for (int pwm : pwmValues) {
            _steps.append({servo, pwm, 800});
        }
    }
    _startTest();
}

void HardwareTestController::abortSequence()
{
    if (!_running) return;

    _stepTimer.stop();
    _finishTest(false, QStringLiteral("Test aborted by user"));
}

void HardwareTestController::_startTest()
{
    _running = true;
    _allPassed = true;
    _sequenceCompleted = false;
    _currentStep = 0;
    _lastError.clear();
    _progress = 0.0;
    _totalSteps = _steps.size();

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

    const Step &step = _steps[_currentStep];
    _progress = static_cast<qreal>(_currentStep) / _totalSteps;
    emit stepProgressChanged();

    if (_isMotorTest) {
        _sendMotorStep(step.instance, step.value, step.durationMs / 1000);
    } else {
        _sendServoStep(step.instance, step.value, step.durationMs / 1000);
    }

    _stepTimer.start(step.durationMs + 500);
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
