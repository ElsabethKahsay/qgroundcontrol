// Checks that GPS altitude and barometric altitude agree within a configurable delta.
// Fails when the two sources diverge beyond the threshold, indicating GPS or baro error.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BaroAltConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    BaroAltConsistencyCheck(TelemetryBridge *telemetry, double maxDeltaM = 5.0,
                            QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;

private:
    double m_maxDeltaM;
};
