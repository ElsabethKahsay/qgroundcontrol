#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BaroTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    BaroTemperatureCheck(TelemetryBridge *telemetry, double maxTempC = 65.0,
                         double minTempC = -10.0, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxTempC;
    double m_minTempC;
};
