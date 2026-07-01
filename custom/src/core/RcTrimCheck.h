#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcTrimCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcTrimCheck(TelemetryBridge *telemetry,
                int centerTolerance = 50,
                QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_centerTolerance;
};
