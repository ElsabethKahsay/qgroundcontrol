// Evaluates GPS speed uncertainty using vel_acc when available, falling back to HDOP and jitter detection.
// Fails on no 3D fix or excessive speed error; warns on high HDOP or repeated speed jumps.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GpsSpeedAccuracyCheck : public AbstractCheck {
    Q_OBJECT
public:
    GpsSpeedAccuracyCheck(TelemetryBridge *telemetry, double maxSpeedErr = 2.0,
                          double maxHdop = 2.0, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxSpeedErr;
    double m_maxHdop;
    double m_prevSpeed = -1.0;
    int m_jitterCount = 0;
};
