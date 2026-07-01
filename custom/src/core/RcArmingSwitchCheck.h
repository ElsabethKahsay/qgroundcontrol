#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcArmingSwitchCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcArmingSwitchCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
