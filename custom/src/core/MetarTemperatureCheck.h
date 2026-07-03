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
