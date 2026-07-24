// Verifies the vehicle heartbeat link is active and connection quality is acceptable.
// Fails when no heartbeat received or connection quality drops below threshold.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class HeartbeatCheck : public AbstractCheck {
    Q_OBJECT
public:
    HeartbeatCheck(TelemetryBridge *telemetry, int timeoutSec = 10,
                   QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_timeoutSec;
};
