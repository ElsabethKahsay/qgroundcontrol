// Validates that ESC PWM outputs are within the expected range (default 800-2200 us).
// Fails when any motor channel outputs 0 or values outside the valid PWM range.
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
