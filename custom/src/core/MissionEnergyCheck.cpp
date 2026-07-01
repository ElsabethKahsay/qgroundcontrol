#include "MissionEnergyCheck.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "PowerModel.h"
#include "TelemetryBridge.h"
#include "VehicleProfileManager.h"

MissionEnergyCheck::MissionEnergyCheck(PowerModel *powerModel,
                                       double batterySafetyFraction,
                                       QObject *parent)
    : AbstractCheck(QStringLiteral("safety.mission.energy"),
                    QStringLiteral("Mission Energy Feasibility"),
                    CheckCategory::Safety,
                    CheckType::Auto,
                    false,   // mandatory = informational/warning
                    false,   // canOverride = factual computation
                    parent)
    , m_powerModel(powerModel)
    , m_batterySafetyFraction(batterySafetyFraction)
{
}

void MissionEnergyCheck::setVehicleProfileManager(VehicleProfileManager *mgr)
{
    m_vehicleProfileMgr = mgr;
}

void MissionEnergyCheck::evaluate()
{
    if (!m_telemetry) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No telemetry"));
        return;
    }

    double batteryCapacityWh = readBatteryCapacityWh();
    if (batteryCapacityWh <= 0) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("Cannot read BATT_CAPACITY parameter"));
        return;
    }

    double missionDistance = estimateMissionDistanceKm();
    if (missionDistance <= 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("No mission loaded — estimate unavailable"));
        return;
    }

    QString deviceUid = m_vehicleProfileMgr ? m_vehicleProfileMgr->currentDeviceUid() : QString();
    double payloadKg = m_vehicleProfileMgr ? m_vehicleProfileMgr->currentPayloadWeightKg() : 0.0;

    // Determine airframe type from vehicle profile
    QString airframe = QStringLiteral("MultiRotor");
    if (m_vehicleProfileMgr && !deviceUid.isEmpty()) {
        QString hist = m_vehicleProfileMgr->currentVehicleHistoryJson();
        if (hist.contains(QStringLiteral("FixedWing")))
            airframe = QStringLiteral("FixedWing");
        else if (hist.contains(QStringLiteral("VTOL")))
            airframe = QStringLiteral("VTOL");
    }

    PowerEstimate est = m_powerModel->estimate(deviceUid, payloadKg,
                                                batteryCapacityWh, missionDistance, airframe);

    // Check if mission is feasible with safety fraction of battery capacity
    double cruiseWh = missionDistance * est.whPerKm;
    double hoverWh = 2.0 * PowerModel::defaultHoverWhPerMin(airframe);
    double requiredWh = cruiseWh + hoverWh;
    double availableWh = batteryCapacityWh * m_batterySafetyFraction;
    double requiredPct = (requiredWh / batteryCapacityWh) * 100.0;

    if (requiredWh > availableWh) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Mission requires %1 Wh (%2% of battery). "
                                 "Safety limit: %3% (%4 Wh). %5")
                      .arg(requiredWh, 0, 'f', 0)
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(m_batterySafetyFraction * 100, 0, 'f', 0)
                      .arg(availableWh, 0, 'f', 0)
                      .arg(est.sourceLabel));
    } else if (requiredPct > 50.0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Mission uses %1% of battery (limit %2%). "
                                 "Est. range: %3 km. %4")
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(m_batterySafetyFraction * 100, 0, 'f', 0)
                      .arg(est.rangeKm, 0, 'f', 1)
                      .arg(est.sourceLabel));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Mission feasible: %1% of battery. "
                                 "Est. range: %2 km. %3")
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(est.rangeKm, 0, 'f', 1)
                      .arg(est.sourceLabel));
    }
}

double MissionEnergyCheck::readBatteryCapacityWh() const
{
    if (!m_telemetry) return -1;

    // Try MAVLink parameter BATT_CAPACITY (mAh)
    double capacityMah = -1.0;
    if (m_telemetry->hasParameter(QStringLiteral("BATT_CAPACITY"))) {
        capacityMah = static_cast<double>(m_telemetry->parameterValue(QStringLiteral("BATT_CAPACITY")));
    }
    if (capacityMah > 0) {
        // Convert mAh to Wh using nominal voltage (3.7V per cell)
        double voltage = m_telemetry->batteryVoltage();
        if (voltage > 0) {
            double cellCount = qRound(voltage / 4.2);
            return capacityMah * cellCount * 3.7 / 1000.0;
        }
        // Fallback: assume 6S (22.2V nominal) if voltage not available
        return capacityMah * 6.0 * 3.7 / 1000.0;
    }

    // Fallback: estimate from battery voltage and typical capacity
    double voltage = m_telemetry->batteryVoltage();
    if (voltage > 0) {
        double cellCount = qRound(voltage / 4.2);
        double estimatedMah = 5000.0; // typical 5000 mAh
        return estimatedMah * cellCount * 3.7 / 1000.0;
    }
    return -1;
}

double MissionEnergyCheck::estimateMissionDistanceKm() const
{
    if (!m_telemetry) return -1;

    int count = m_telemetry->missionCount();
    if (count < 2) return -1;

    double firstWpDist = m_telemetry->missionFirstWpDistance();
    if (firstWpDist > 0) {
        // Rough: assume average leg = first leg * 0.7, legs = count - 1
        double avgLeg = firstWpDist * 0.7;
        return (avgLeg * (count - 1)) / 1000.0;
    }

    // No first WP distance: assume 100m average leg
    return (100.0 * (count - 1)) / 1000.0;
}

QString MissionEnergyCheck::getRationale() const
{
    return QStringLiteral("Estimates whether the mission is feasible with "
                          "the current battery capacity and payload, using "
                          "a power consumption model.");
}

QStringList MissionEnergyCheck::getFixSteps() const
{
    return {
        QStringLiteral("Reduce payload weight"),
        QStringLiteral("Use a higher-capacity battery"),
        QStringLiteral("Shorten the mission route"),
    };
}

QString MissionEnergyCheck::getThreshold() const
{
    return QStringLiteral("Mission consumption < %1% of battery capacity")
        .arg(m_batterySafetyFraction * 100, 0, 'f', 0);
}

QString MissionEnergyCheck::getCurrentValueString() const
{
    if (!m_telemetry) return QStringLiteral("No telemetry");
    return QStringLiteral("%1 waypoints, battery %2%")
        .arg(m_telemetry->missionCount())
        .arg(m_telemetry->batteryPercent());
}
