#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MotorTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    MotorTemperatureCheck(TelemetryBridge *telemetry, double maxTempC = 80.0,
                          QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_maxTempC;
};
