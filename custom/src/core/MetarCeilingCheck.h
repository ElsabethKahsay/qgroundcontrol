// Checks cloud ceiling height from METAR against configured and absolute minimums.
// Fails when ceiling is below 60 m absolute minimum; warns below configured threshold.
#pragma once
#include "AbstractCheck.h"

class MetarCeilingCheck : public AbstractCheck {
    Q_OBJECT
public:
    MetarCeilingCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
