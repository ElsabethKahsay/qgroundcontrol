// Checks that ESC-reported voltages are consistent with the battery voltage.
// Fails when any ESC voltage deviates from battery voltage by more than maxDeltaV (default 0.5 V).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EscVoltageConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    EscVoltageConsistencyCheck(TelemetryBridge *telemetry, double maxDeltaV = 0.5,
                               QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_maxDeltaV;
};
