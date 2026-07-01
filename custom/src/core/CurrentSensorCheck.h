#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CurrentSensorCheck : public AbstractCheck {
    Q_OBJECT
public:
    CurrentSensorCheck(TelemetryBridge *telemetry,
                       double maxDisarmedCurrent = 0.5,
                       double minArmedCurrent = 0.5,
                       QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxDisarmedCurrent;
    double m_minArmedCurrent;
};
