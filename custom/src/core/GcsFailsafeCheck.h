// Verifies the GCS (ground control station) failsafe parameter is enabled.
// Warns when FS_GCS_ENABLE is 0 (no failsafe action on GCS link loss).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GcsFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    GcsFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
