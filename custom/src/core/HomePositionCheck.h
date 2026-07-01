#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class HomePositionCheck : public AbstractCheck {
    Q_OBJECT
public:
    HomePositionCheck(TelemetryBridge *telemetry,
                      double maxDistKm = 0.005,
                      QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxDistKm;
};
