#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EscFirmwareCheck : public AbstractCheck {
    Q_OBJECT
public:
    EscFirmwareCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
