#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompassCalCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompassCalCheck(TelemetryBridge *telemetry,
                    int maxDeviation = 150, QObject *parent = nullptr);
    void evaluate() override;

private:
    int m_maxDeviation;
};
