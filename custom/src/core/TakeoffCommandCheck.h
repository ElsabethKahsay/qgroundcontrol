#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class TakeoffCommandCheck : public AbstractCheck {
    Q_OBJECT
public:
    TakeoffCommandCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
