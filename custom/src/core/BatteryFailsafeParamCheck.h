#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BatteryFailsafeParamCheck : public AbstractCheck {
    Q_OBJECT
public:
    explicit BatteryFailsafeParamCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
