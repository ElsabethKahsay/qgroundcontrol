// Compares GPS altitude with barometric relative altitude to detect sensor disagreement.
// Warns when the absolute delta exceeds the configured meter limit.
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
