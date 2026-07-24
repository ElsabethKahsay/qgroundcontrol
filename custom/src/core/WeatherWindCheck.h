// Checks sustained wind speed and gusts from live METAR data against configured thresholds.
// Fails when sustained or gust wind exceeds thresholds (strong fail at 125% of threshold).
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
