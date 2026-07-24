// Checks battery temperature stays within safe operating range using a 3-second moving average.
// Fails when temperature exceeds the max or drops below the min threshold.
#pragma once
#include "AbstractCheck.h"
#include "Hysteresis.h"

class TelemetryBridge;

class BatteryTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    BatteryTemperatureCheck(TelemetryBridge *telemetry,
                            double maxTemp = 45.0,
                            double minTemp = 0.0,
                            QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxTemp;
    double m_minTemp;
    AveragingFilter<double> m_avgFilter{3000}; // 3-second moving average
};
