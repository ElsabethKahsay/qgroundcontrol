// Checks gimbal presence, mode, and associated video stream status.
// Fails when gimbal is detected but no video stream is active.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GimbalLinkCheck : public AbstractCheck {
    Q_OBJECT
public:
    GimbalLinkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
