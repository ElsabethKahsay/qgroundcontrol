// Verifies the compass rotation parameter matches expected orientation from the vehicle profile.
// Auto-passes for default rotation or profile match; otherwise prompts operator to confirm.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompassOrientationCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompassOrientationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    void applyVehicleConfig(const QJsonObject &config) override;

private:
    int m_expectedRotation = -1; // -1 = not configured per vehicle
};
