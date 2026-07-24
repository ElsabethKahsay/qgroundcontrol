// Verifies battery cell count matches configured parameter and all cell voltages are above minimum.
// Fails on cell count mismatch, individual cell under-voltage, or voltage-derived cell count discrepancy.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class CellConfigCheck : public AbstractCheck {
    Q_OBJECT
public:
    CellConfigCheck(TelemetryBridge *telemetry,
                    double minCellVoltage = 3.0,
                    int maxCellDelta = 1,
                    QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    double m_minCellVoltage;
    int m_maxCellDelta;
};
