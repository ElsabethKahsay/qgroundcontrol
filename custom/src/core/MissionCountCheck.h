#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MissionCountCheck : public AbstractCheck {
    Q_OBJECT
public:
    MissionCountCheck(TelemetryBridge *telemetry, int minMissionCount = 1, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_minMissionCount;
};
