// Checks for gusty or erratic wind conditions, auto-passing if METAR gusts are within limits.
// Fails when no fresh METAR is available or gusts exceed the configured threshold.
#pragma once
#include "AbstractCheck.h"

class WeatherWindGustCheck : public AbstractCheck {
    Q_OBJECT
public:
    WeatherWindGustCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
