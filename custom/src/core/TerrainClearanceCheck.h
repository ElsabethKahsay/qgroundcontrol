#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class TerrainClearanceCheck : public AbstractCheck {
    Q_OBJECT
public:
    TerrainClearanceCheck(TelemetryBridge *telemetry, double minTerrainClearance = 5.0, QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_minTerrainClearance;
};
