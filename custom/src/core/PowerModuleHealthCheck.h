#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class PowerModuleHealthCheck : public AbstractCheck {
    Q_OBJECT
public:
    PowerModuleHealthCheck(TelemetryBridge *telemetry, double maxDeltaV = 0.3,
                           QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;

private:
    double m_maxDeltaV;
};
