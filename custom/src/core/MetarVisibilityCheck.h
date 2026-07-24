// Checks METAR visibility distance against configured threshold and absolute minimum.
// Fails when visibility is below 1 km minimum; warns below configured threshold.
#pragma once
#include "AbstractCheck.h"

class MetarVisibilityCheck : public AbstractCheck {
    Q_OBJECT
public:
    MetarVisibilityCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
