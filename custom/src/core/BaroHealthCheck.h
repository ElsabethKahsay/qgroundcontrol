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
