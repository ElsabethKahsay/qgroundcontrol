#pragma once
#include "AbstractCheck.h"

class MetarVisibilityCheck : public AbstractCheck {
    Q_OBJECT
public:
    MetarVisibilityCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
