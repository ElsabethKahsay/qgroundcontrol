#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class AhrsHealthCheck : public AbstractCheck {
    Q_OBJECT
public:
    AhrsHealthCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
