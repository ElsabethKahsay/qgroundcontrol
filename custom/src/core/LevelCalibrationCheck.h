#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class LevelCalibrationCheck : public AbstractCheck {
    Q_OBJECT
public:
    LevelCalibrationCheck(TelemetryBridge *telemetry,
                          double maxPitchRollDeg = 2.0,
                          QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxPitchRollDeg;
};
