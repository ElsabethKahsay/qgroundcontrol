#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BatteryFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    BatteryFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
