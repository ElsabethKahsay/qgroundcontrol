// Manual check requiring each motor to be individually spin-tested via the motor test panel.
// Stays pending until the user completes all motor tests; fails if vehicle is armed.
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
