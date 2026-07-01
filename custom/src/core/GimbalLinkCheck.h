#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GimbalLinkCheck : public AbstractCheck {
    Q_OBJECT
public:
    GimbalLinkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
