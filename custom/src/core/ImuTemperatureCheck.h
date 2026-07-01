#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class ImuTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    ImuTemperatureCheck(TelemetryBridge *telemetry, double maxTemp,
                        double minTemp, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxTemp;
    double m_minTemp;
};
