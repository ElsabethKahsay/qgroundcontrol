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
