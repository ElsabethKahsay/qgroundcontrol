// Checks IMU vibration levels on all three axes against a configurable threshold.
// Fails when any axis exceeds the limit or sensor clipping is detected.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class VibrationCheck : public AbstractCheck {
    Q_OBJECT
public:
    VibrationCheck(TelemetryBridge *telemetry,
                   double maxVibration = 30.0,
                   QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;
private:
    double m_maxVibration;
};
