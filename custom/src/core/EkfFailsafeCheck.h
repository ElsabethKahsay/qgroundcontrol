#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EkfFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    EkfFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
