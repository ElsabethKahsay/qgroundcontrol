#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CompanionLinkCheck : public AbstractCheck {
    Q_OBJECT
public:
    CompanionLinkCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
