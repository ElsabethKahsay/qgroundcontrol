#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class GpsFixCheck : public AbstractCheck {
    Q_OBJECT
public:
    GpsFixCheck(TelemetryBridge *telemetry, int minSatellites = 8,
                double maxHdop = 2.0, QObject *parent = nullptr);
    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    int m_minSatellites;
    double m_maxHdop;
};
