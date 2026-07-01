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
