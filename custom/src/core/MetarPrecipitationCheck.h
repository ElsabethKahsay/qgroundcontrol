// Checks METAR reports for precipitation (rain, snow, thunderstorms, etc.).
// Warns whenever any precipitation is reported; flags severe types (TS, GR, SN, etc.).
#pragma once
#include "AbstractCheck.h"

class MetarPrecipitationCheck : public AbstractCheck {
    Q_OBJECT
public:
    MetarPrecipitationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
