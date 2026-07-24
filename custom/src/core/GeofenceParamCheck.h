// Validates geofence configuration: enabled state, fence type, action, and distance/altitude limits.
// Warns when geofence is enabled but missing altitude or lateral fence types or no action is set.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GeofenceParamCheck : public AbstractCheck {
    Q_OBJECT
public:
    GeofenceParamCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
