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
