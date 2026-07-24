// Checks EKF velocity, horizontal/vertical position, and compass variance values against thresholds.
// Fails when any variance exceeds its limit (preferring vehicle COM_ARM_EKF_* params over defaults).
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EkfVarianceCheck : public AbstractCheck {
    Q_OBJECT
public:
    EkfVarianceCheck(TelemetryBridge *telemetry,
                     double maxVelVariance = 0.5,
                     double maxPosHorizVariance = 5.0,
                     double maxPosVertVariance = 5.0,
                     QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    double m_maxVelVariance;
    double m_maxPosHorizVariance;
    double m_maxPosVertVariance;
};
