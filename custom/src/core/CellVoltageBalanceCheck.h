#pragma once
#include "AbstractCheck.h"
#include "Hysteresis.h"

class TelemetryBridge;

class CellVoltageBalanceCheck : public AbstractCheck {
    Q_OBJECT
public:
    CellVoltageBalanceCheck(TelemetryBridge *telemetry,
                            double maxDelta = 0.15,
                            QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxDelta;
    AveragingFilter<double> m_deltaAvgFilter{5000}; // 5-second moving average for cell delta
};
