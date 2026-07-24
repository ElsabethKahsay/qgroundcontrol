// Verifies the vehicle is not tilted beyond safe limits before takeoff.
// Fails when roll or pitch exceeds the maximum angle, indicating the vehicle is not level.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AttitudeCheck : public AbstractCheck {
    Q_OBJECT
public:
    AttitudeCheck(TelemetryBridge *telemetry, double maxPitchRollDeg = 30.0, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxPitchRollDeg;
};
