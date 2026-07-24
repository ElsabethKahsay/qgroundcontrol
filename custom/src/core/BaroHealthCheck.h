// Verifies the barometer sensor is healthy via SYS_STATUS and reports valid pressure/temp.
// Fails on sensor health faults, missing data, or out-of-range temperature.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BaroHealthCheck : public AbstractCheck {
    Q_OBJECT
public:
    BaroHealthCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
};
