#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class PreArmOkCheck : public AbstractCheck {
    Q_OBJECT
public:
    PreArmOkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
