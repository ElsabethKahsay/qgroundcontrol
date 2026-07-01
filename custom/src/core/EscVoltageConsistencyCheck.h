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
