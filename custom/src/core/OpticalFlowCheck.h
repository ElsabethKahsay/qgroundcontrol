#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class OpticalFlowCheck : public AbstractCheck {
    Q_OBJECT
public:
    OpticalFlowCheck(TelemetryBridge *telemetry, int minQuality,
                     QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_minQuality;
};
