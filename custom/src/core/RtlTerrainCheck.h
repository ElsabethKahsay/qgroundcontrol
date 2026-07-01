#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RtlTerrainCheck : public AbstractCheck {
    Q_OBJECT
public:
    RtlTerrainCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
