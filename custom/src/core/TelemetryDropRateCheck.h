// Monitors telemetry packet drop rate against warning and failure thresholds.
// Fails when drop rate exceeds maxDropRate; warns between warnDropRate and maxDropRate.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class TelemetryDropRateCheck : public AbstractCheck {
    Q_OBJECT
public:
    TelemetryDropRateCheck(TelemetryBridge *telemetry,
                            uint maxDropRate = 10,
                            uint warnDropRate = 5,
                           QObject *parent = nullptr);
    void evaluate() override;

private:
    uint m_maxDropRate;
    uint m_warnDropRate;
};
