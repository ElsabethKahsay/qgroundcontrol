// Checks AHRS estimator health via SYS_STATUS sensor bitmask and dedicated health flag.
// Warns if attitude stabilization sensor is unhealthy or AHRS reports degraded state.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AhrsHealthCheck : public AbstractCheck {
    Q_OBJECT
public:
    AhrsHealthCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
