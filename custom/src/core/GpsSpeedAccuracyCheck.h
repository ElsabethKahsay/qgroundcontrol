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
