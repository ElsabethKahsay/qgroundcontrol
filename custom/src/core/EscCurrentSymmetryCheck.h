#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EscCurrentSymmetryCheck : public AbstractCheck {
    Q_OBJECT
public:
    EscCurrentSymmetryCheck(TelemetryBridge *telemetry, double maxDevRatio = 0.20,
                            QObject *parent = nullptr);
    void evaluate() override;
private:
    double m_maxDevRatio;
};
