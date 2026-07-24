// Checks that the RC receiver has not entered failsafe mode and signal is above minimum RSSI.
// Fails when the vehicle reports an RC failsafe condition or RSSI drops below the threshold.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcFailsafeCheck(TelemetryBridge *telemetry, int minRssi = 50, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_minRssi;
};
