#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class DualGpsConsistencyCheck : public AbstractCheck {
    Q_OBJECT
public:
    DualGpsConsistencyCheck(TelemetryBridge *telemetry, double maxDivergenceM = 2.0,
                            QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_maxDivergenceM;
    static double _haversineM(double lat1, double lon1, double lat2, double lon2);
};
