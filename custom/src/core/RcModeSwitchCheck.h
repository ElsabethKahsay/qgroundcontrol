#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcModeSwitchCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcModeSwitchCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
