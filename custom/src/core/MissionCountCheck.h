// Checks that the uploaded mission contains at least the required minimum number of items.
// Fails if item count is non-zero but below minimum; stays pending when no mission is loaded.
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
