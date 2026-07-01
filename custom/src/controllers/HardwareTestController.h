#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

class Vehicle;

class HardwareTestController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(bool allPassed READ allPassed NOTIFY allPassedChanged)
    Q_PROPERTY(QVariant sequenceCompleted READ sequenceCompleted NOTIFY sequenceCompletedChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorMessageChanged)
    Q_PROPERTY(qreal stepProgress READ stepProgress NOTIFY stepProgressChanged)

public:
    explicit HardwareTestController(QObject *parent = nullptr);

    bool isRunning() const { return _running; }
    bool allPassed() const { return _allPassed; }
    QVariant sequenceCompleted() const;
    QString lastErrorMessage() const { return _lastError; }
    qreal stepProgress() const { return _progress; }

    void setVehicle(Vehicle *vehicle);

    Q_INVOKABLE void runMotorTest();
    Q_INVOKABLE void runServoSweep();
    Q_INVOKABLE void abortSequence();

signals:
    void isRunningChanged();
    void allPassedChanged();
    void sequenceCompletedChanged();
    void lastErrorMessageChanged();
    void stepProgressChanged();

private slots:
    void _advanceStep();
    void _onCommandResult(int cmdId, int compId, int mavResult);

private:
    void _startTest();
    void _finishTest(bool passed, const QString &error);
    void _sendMotorStep(int motorInstance, int throttlePct, int durationSec);
    void _sendServoStep(int servoInstance, int pwmValue, int durationSec);

    Vehicle *_vehicle = nullptr;
    bool _running = false;
    bool _allPassed = false;
    bool _sequenceCompleted = false;
    int _currentStep = 0;
    int _totalSteps = 0;
    qreal _progress = 0.0;
    QString _lastError;
    bool _isMotorTest = false;
    QTimer _stepTimer;

    struct Step {
        int instance;
        int value;
        int durationMs;
    };
    QVector<Step> _steps;
};
