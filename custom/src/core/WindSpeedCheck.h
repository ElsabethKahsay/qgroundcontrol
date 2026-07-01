#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class WindSpeedCheck : public AbstractCheck {
    Q_OBJECT
public:
    WindSpeedCheck(TelemetryBridge *telemetry,
                   double maxWindMps = 10.0,
                   QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxWindMps;
};
