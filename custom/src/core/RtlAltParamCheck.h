// Verifies the RTL return-to-launch altitude is within a safe range (between min and max).
// Warns if the altitude is too low (risk of obstacle strike) or exceeds the maximum recommendation.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RtlAltParamCheck : public AbstractCheck {
    Q_OBJECT
public:
    RtlAltParamCheck(TelemetryBridge *telemetry,
                     double minAlt = 10.0, double maxAlt = 122.0,
                     QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_minAlt;
    double m_maxAlt;
};
