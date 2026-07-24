// Checks TAF (Terminal Aerodrome Forecast) for deteriorating weather conditions.
// Warns when the TAF predicts worsening conditions within the next 6 hours.
#pragma once
#include "AbstractCheck.h"

class TafDeteriorationCheck : public AbstractCheck {
    Q_OBJECT
public:
    TafDeteriorationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
