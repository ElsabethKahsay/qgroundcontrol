// Reads the vehicle's aggregated pre-arm check status to verify all internal checks have passed.
// Stays pending until the vehicle reports pre-arm OK, showing the highest-severity failure message.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class PreArmOkCheck : public AbstractCheck {
    Q_OBJECT
public:
    PreArmOkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
