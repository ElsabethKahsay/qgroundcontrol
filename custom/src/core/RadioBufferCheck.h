// Checks that radio TX buffer level is above a minimum threshold.
// Fails when buffer drops below minTxBuf, indicating potential radio congestion or range loss.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RadioBufferCheck : public AbstractCheck {
    Q_OBJECT
public:
    RadioBufferCheck(TelemetryBridge *telemetry, int minTxBuf = 20, QObject *parent = nullptr);
    void evaluate() override;
private:
    int m_minTxBuf;
};
