// Verifies the home position is set and close to the current GPS location.
// Fails when home is not set or is farther than maxDistKm from the vehicle.
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
