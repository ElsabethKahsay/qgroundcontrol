#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcChannelCountCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcChannelCountCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
