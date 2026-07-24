// Checks that RTL is configured for terrain-following altitude mode and valid cone slope.
// Fails when RTL_ALT_TYPE is relative (not terrain) or RTL_CONE_SLOPE is zero/disabled.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RtlTerrainCheck : public AbstractCheck {
    Q_OBJECT
public:
    RtlTerrainCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
