#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GpsBaroAltConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    GpsBaroAltConsistencyCheck(TelemetryBridge *telemetry,
                               double maxDeltaM = 10.0,
                               QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_maxDeltaM;
};
