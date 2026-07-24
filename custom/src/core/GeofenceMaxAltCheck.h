// Verifies the geofence maximum altitude parameter (GF_MAX_ALT) is set to a reasonable value.
// Warns if the altitude ceiling is below the minimum or set to zero (no limit).
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
