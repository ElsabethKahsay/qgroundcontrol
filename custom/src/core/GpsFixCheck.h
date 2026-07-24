// Checks GPS has a 3D fix with sufficient satellites and acceptable HDOP.
// Fails on no fix, 2D-only fix, too few satellites, or HDOP above threshold.
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
