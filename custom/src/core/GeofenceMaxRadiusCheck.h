#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GeofenceMaxRadiusCheck : public AbstractCheck {
    Q_OBJECT
public:
    GeofenceMaxRadiusCheck(TelemetryBridge *telemetry, double minRadius = 10.0,
                           QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_minRadius;
};
