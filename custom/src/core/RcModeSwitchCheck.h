// Checks that the RC flight-mode switch channel is reporting a valid PWM signal.
// Warns when the mode channel value is missing, out of range, or signal is lost.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcModeSwitchCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcModeSwitchCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
