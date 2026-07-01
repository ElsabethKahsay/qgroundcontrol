#pragma once
#include "AbstractCheck.h"

class PowerModel;
class VehicleProfileManager;

class MissionEnergyCheck : public AbstractCheck {
    Q_OBJECT
public:
    MissionEnergyCheck(PowerModel *powerModel,
                       double batterySafetyFraction = 0.60,
                       QObject *parent = nullptr);

    void setVehicleProfileManager(VehicleProfileManager *mgr);

    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

private:
    double readBatteryCapacityWh() const;
    double estimateMissionDistanceKm() const;

    PowerModel *m_powerModel = nullptr;
    VehicleProfileManager *m_vehicleProfileMgr = nullptr;
    double m_batterySafetyFraction;
};
