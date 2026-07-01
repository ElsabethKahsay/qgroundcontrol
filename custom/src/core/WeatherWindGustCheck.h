#pragma once
#include "AbstractCheck.h"

class WeatherWindGustCheck : public AbstractCheck {
    Q_OBJECT
public:
    WeatherWindGustCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
