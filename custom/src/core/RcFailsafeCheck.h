#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcFailsafeCheck(TelemetryBridge *telemetry, int minRssi = 50, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_minRssi;
};
