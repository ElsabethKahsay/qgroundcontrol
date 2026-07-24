// Verifies optical flow sensor quality is above a minimum threshold.
// Fails when quality is below minimum, indicating the sensor may be obstructed or non-functional.
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
