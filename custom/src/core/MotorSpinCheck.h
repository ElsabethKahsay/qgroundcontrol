#pragma once
#include "AbstractCheck.h"
#include <QTimer>

class TelemetryBridge;

class MotorSpinCheck : public AbstractCheck {
    Q_OBJECT
public:
    MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    void reset() override;

private slots:
    void _runNextMotor();

private:
    bool _checkMotorFeedback(int motorIndex = -1);
    void _startTestSequence();

    int m_currentMotor = 0;
    bool m_testCompleted = false;
    QTimer m_testTimer;
};
