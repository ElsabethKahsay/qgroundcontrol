#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MotorCountCheck : public AbstractCheck {
    Q_OBJECT
public:
    MotorCountCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    int expectedMotorCount() const;
};
