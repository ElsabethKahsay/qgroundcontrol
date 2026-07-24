// Verifies the number of RC channels received matches the expected count (RC_CHAN_CNT).
// Fails when fewer channels are received than configured, indicating RC signal issues.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcChannelCountCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcChannelCountCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
