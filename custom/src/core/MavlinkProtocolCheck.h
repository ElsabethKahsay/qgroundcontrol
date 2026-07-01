#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MavlinkProtocolCheck : public AbstractCheck {
    Q_OBJECT
public:
    MavlinkProtocolCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
