// Checks ESC firmware status via ESC_INFO failure flags and error counts.
// Warns when any ESC reports hard failure flags; passes with error counts otherwise.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EscFirmwareCheck : public AbstractCheck {
    Q_OBJECT
public:
    EscFirmwareCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
