// Checks whether a companion computer is detected and responding via heartbeat.
// Skips if no companion is detected; passes when heartbeat is received.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompanionLinkCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompanionLinkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
