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
