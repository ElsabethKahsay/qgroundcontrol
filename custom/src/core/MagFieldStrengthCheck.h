// Checks total magnetic field magnitude is within expected range for the local geomagnetic field.
// Fails when the field strength falls outside min/max bounds, indicating possible interference.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MagFieldStrengthCheck : public AbstractCheck {
    Q_OBJECT
public:
    MagFieldStrengthCheck(TelemetryBridge *telemetry,
                          double minGauss = 0.15, double maxGauss = 0.65,
                          QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
private:
    double m_minGauss;
    double m_maxGauss;
};
