#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GeofenceMaxAltCheck : public AbstractCheck {
    Q_OBJECT
public:
    GeofenceMaxAltCheck(TelemetryBridge *telemetry, double minAlt = 10.0,
                        QObject *parent = nullptr);
    void evaluate() override;

private:
    double m_minAlt;
};
