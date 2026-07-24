// Checks that ESC current draw is balanced across all motors within a deviation ratio.
// Warns when any ESC current deviates more than maxDevRatio (default 20%) from the mean.
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
