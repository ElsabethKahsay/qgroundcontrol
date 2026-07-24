// Checks the MAVLink protocol version reported by the vehicle.
// Fails on version 0 (no heartbeat) and warns if version < 3 (MAVLink 1).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MavlinkProtocolCheck : public AbstractCheck {
    Q_OBJECT
public:
    MavlinkProtocolCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
