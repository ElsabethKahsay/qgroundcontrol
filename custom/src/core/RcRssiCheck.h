// Checks that both RC and radio link signal strengths are above their minimum thresholds.
// Fails when channel data is stale or both links are weak; warns if only one link is below threshold.
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
