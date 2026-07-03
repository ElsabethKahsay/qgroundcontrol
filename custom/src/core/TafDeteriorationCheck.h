#pragma once
#include "AbstractCheck.h"

class TafDeteriorationCheck : public AbstractCheck {
    Q_OBJECT
public:
    TafDeteriorationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
private:
    bool m_fetchTriggered = false;
};
