#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MotorSpinCheck : public AbstractCheck {
    Q_OBJECT
public:
    MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    void reset() override;
};
