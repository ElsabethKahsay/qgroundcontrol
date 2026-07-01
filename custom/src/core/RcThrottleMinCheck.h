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
