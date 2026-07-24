// Checks compass sensor health flag and data quality level from the flight controller.
// Warns if compass is unhealthy or data quality is below acceptable threshold.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompassCalCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompassCalCheck(TelemetryBridge *telemetry,
                    int maxDeviation = 150, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_maxDeviation;
};
