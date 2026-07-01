#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class VibrationFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    VibrationFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
