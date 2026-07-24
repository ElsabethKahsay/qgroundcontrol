// Verifies battery failsafe parameters are configured with a reasonable threshold and action.
// Fails if threshold is out of range or action is missing; warns if failsafe is disabled.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BatteryFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    BatteryFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
