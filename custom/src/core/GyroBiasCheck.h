// Checks that gyroscope bias magnitude is below a configurable threshold.
// Fails when bias vector magnitude exceeds maxBiasRadS, indicating poor IMU calibration.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GyroBiasCheck : public AbstractCheck {
    Q_OBJECT
public:
    GyroBiasCheck(TelemetryBridge *telemetry,
                  double maxBiasRadS = 0.05,
                  QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxBiasRadS;
};
