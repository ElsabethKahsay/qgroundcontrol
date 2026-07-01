#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GcsFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    GcsFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
