#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompassOrientationCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompassOrientationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
