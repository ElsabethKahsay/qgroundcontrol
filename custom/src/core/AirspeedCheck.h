// Checks that airspeed reading meets minimum for safe takeoff.
// Fails when airspeed is below minimum or sensor returns zero (not fitted/invalid).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AirspeedCheck : public AbstractCheck {
    Q_OBJECT
public:
    AirspeedCheck(TelemetryBridge *telemetry, double minAirspeed = 20.0,
                  QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_minAirspeed;
};
