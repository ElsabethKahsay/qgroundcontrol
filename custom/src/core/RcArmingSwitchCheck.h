// Checks that arming via RC is enabled (ARMING_RC_ENABLE parameter).
// Fails when RC arming is disabled, preventing the pilot from arming with the transmitter.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcArmingSwitchCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcArmingSwitchCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
