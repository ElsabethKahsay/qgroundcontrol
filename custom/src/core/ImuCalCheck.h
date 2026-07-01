#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class ImuCalCheck : public AbstractCheck {
    Q_OBJECT
public:
    ImuCalCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
