#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompassYawConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompassYawConsistencyCheck(TelemetryBridge *telemetry,
                                double maxDeviationDeg = 15.0,
                               QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_maxDeviationDeg;
};
