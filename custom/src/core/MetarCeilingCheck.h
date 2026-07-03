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
