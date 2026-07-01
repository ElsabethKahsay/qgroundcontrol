#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcCalibrationCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcCalibrationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
