// Validates the first waypoint distance from home is within a safe maximum.
// Warns if the first waypoint is farther than the configured limit from the home position.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MissionItemCheck : public AbstractCheck {
    Q_OBJECT
public:
    MissionItemCheck(TelemetryBridge *telemetry, double maxFirstWpDistM = 10000.0, QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxFirstWpDistM;
};
