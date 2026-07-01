#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class BatteryVoltageCheck : public AbstractCheck {
    Q_OBJECT
public:
    BatteryVoltageCheck(TelemetryBridge *telemetry,
                        double minVoltage = 0.0,
                        double minPercent = 5.0,
                        QObject *parent = nullptr);

    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    double m_minVoltage;
    double m_minPercent;
    double effectiveMinVoltage() const;
    double effectiveMinPercent() const;
    int effectiveCellCount() const;
    double estimateCellCount() const;
};
