#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EscResponsivenessCheck : public AbstractCheck {
    Q_OBJECT
public:
    EscResponsivenessCheck(TelemetryBridge *telemetry,
                           uint16_t minPwm = 800, uint16_t maxPwm = 2200,
                           QObject *parent = nullptr);
    void evaluate() override;
private:
    uint16_t m_minPwm;
    uint16_t m_maxPwm;
};
