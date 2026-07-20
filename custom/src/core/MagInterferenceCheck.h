#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class MagInterferenceCheck : public AbstractCheck {
    Q_OBJECT
public:
    MagInterferenceCheck(TelemetryBridge *telemetry,
                         double maxDeltaGauss = 0.20,
                         double throttleThreshold = 0.15,
                         QObject *parent = nullptr);
    void evaluate() override;

private:
    double magMagnitude() const;
    double throttlePercent() const;

    double m_maxDeltaGauss;
    double m_throttleThreshold;

    double m_baselineMag = 0.0;
    int m_baselineSamples = 0;
};
