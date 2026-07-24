// Verifies that a secondary (redundant) battery is present and above minimum voltage.
// Skips if no secondary battery is detected; fails if its voltage is too low.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RedundantPowerCheck : public AbstractCheck {
    Q_OBJECT
public:
    RedundantPowerCheck(TelemetryBridge *telemetry, double minVoltage = 10.0,
                        QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;

private:
    double m_minVoltage;
};
