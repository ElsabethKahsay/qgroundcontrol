#pragma once
#include "AbstractCheck.h"

class WeatherWindCheck : public AbstractCheck {
    Q_OBJECT
public:
    WeatherWindCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
