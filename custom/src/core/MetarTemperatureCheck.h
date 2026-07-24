// Checks ambient temperature from METAR against configurable min/max operating range.
// Fails when temperature is outside the safe operating range (default -10 C to 50 C).
#pragma once
#include "AbstractCheck.h"

class MetarTemperatureCheck : public AbstractCheck {
    Q_OBJECT
public:
    MetarTemperatureCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
