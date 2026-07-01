#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GeofenceParamCheck : public AbstractCheck {
    Q_OBJECT
public:
    GeofenceParamCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
