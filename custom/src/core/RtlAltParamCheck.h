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
