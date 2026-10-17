// Checks gimbal presence, mode, and associated video stream status.
// Advisory: warns (never blocks arming) when a gimbal is detected without a
// live video stream.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GimbalLinkCheck : public AbstractCheck {
    Q_OBJECT
public:
    GimbalLinkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
