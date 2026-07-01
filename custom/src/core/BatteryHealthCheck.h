#pragma once
#include "AbstractCheck.h"

class VehicleProfileManager;

class BatteryHealthCheck : public AbstractCheck {
    Q_OBJECT
public:
    BatteryHealthCheck(VehicleProfileManager *profileManager,
                       double capacityWarningPct = 80.0,
                       double sagWarningV = 0.5,
                       QObject *parent = nullptr);

    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    VehicleProfileManager *m_profileManager = nullptr;
    double m_capacityWarningPct;
    double m_sagWarningV;
};
