// Verifies radio failsafe (FS_THR_ENABLE) is enabled on the vehicle.
// Fails when radio failsafe is disabled, leaving no safety fallback on signal loss.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RadioFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    RadioFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
