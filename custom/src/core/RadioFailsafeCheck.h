#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RadioFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    RadioFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
