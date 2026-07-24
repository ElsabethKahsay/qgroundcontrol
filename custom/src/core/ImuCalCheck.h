// Verifies IMU calibration health: checks gyro/accel sensor health bits and data quality.
// Fails when sensors report unhealthy or data quality drops below minimum.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class ImuCalCheck : public AbstractCheck {
    Q_OBJECT
public:
    ImuCalCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
