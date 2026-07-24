// Verifies the VIBE_ACTION parameter is enabled so the vehicle can respond to excessive vibration.
// Warns when VIBE_ACTION is 0 (vibration failsafe disabled).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class VibrationFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    VibrationFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
