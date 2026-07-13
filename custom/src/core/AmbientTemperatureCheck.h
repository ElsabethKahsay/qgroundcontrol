#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AmbientTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    AmbientTemperatureCheck(TelemetryBridge *telemetry, double maxTempC = 50.0,
                            double minTempC = -10.0, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxTempC;
    double m_minTempC;
};
