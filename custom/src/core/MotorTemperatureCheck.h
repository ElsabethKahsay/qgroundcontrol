// Checks ESC-reported motor temperatures against a configurable maximum (default 80 C).
// Fails when any motor exceeds the temperature threshold.
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
