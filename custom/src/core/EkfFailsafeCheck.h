// Verifies the EKF failsafe action parameter is configured and enabled.
// Warns if FS_EKF_ACTION is 0 (disabled); fails if misconfigured with an unexpected value.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EkfFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    EkfFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
