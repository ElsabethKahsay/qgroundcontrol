// Checks that the uploaded mission contains a MAV_CMD_NAV_TAKEOFF command.
// Warns if no takeoff item is found, as RTL may ascend at the default rate instead.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class TakeoffCommandCheck : public AbstractCheck {
    Q_OBJECT
public:
    TakeoffCommandCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
