#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AccelConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    AccelConsistencyCheck(TelemetryBridge *telemetry,
                          double maxDiff = 4.0,
                          QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxDiff;
};
