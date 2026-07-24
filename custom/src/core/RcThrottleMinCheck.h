// Checks that the RC throttle channel is at its minimum (stick-down) position.
// Fails if throttle is above minimum; warns if there is no RC signal on the throttle channel.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcThrottleMinCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcThrottleMinCheck(TelemetryBridge *telemetry,
                       QObject *parent = nullptr);
    void evaluate() override;
};
