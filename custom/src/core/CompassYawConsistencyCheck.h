// Compares compass heading against GPS ground course to detect magnetic deviation.
// Fails when the angular difference exceeds the configured degree threshold.
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
