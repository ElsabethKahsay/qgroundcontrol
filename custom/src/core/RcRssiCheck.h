#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcRssiCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcRssiCheck(TelemetryBridge *telemetry, int minRcRssi = 50,
                int minRadioRssi = 50, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_minRcRssi;
    int m_minRadioRssi;
};
