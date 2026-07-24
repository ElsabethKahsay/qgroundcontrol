#include "MissionEnergyCheck.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

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
                    false,
                    false,
                    parent)
    , m_powerModel(powerModel)
    , m_batterySafetyFraction(batterySafetyFraction)
{
}

void MissionEnergyCheck::setVehicleProfileManager(VehicleProfileManager *mgr)
{
    m_vehicleProfileMgr = mgr;
}

// Estimates required energy (cruise + 2 min hover for takeoff/landing) and compares to battery capacity.
// Fails if required Wh > safety fraction of battery; warns if >50%; passes if within safe margins.
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

    // ── Airframe detection ──
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

    // ── Read MOT_THST_HOVER for actual hover throttle ──
    double hoverThrottle = 0.0;
    bool haveHoverThrottle = false;
    if (isParamAvailable(QStringLiteral("MOT_THST_HOVER"))) {
        hoverThrottle = getTelemetryDouble(QStringLiteral("MOT_THST_HOVER"));
        if (hoverThrottle > 0.01) {
            haveHoverThrottle = true;
        }
    } else if (m_telemetry->hasParameter(QStringLiteral("MOT_THST_HOVER"))) {
        hoverThrottle = static_cast<double>(m_telemetry->parameterValue(QStringLiteral("MOT_THST_HOVER")));
        if (hoverThrottle > 0.01) {
            haveHoverThrottle = true;
        }
    }

    // Hover power: use actual param if available, otherwise default
    double hoverWhPerMin;
    if (haveHoverThrottle) {
        // Scale default hover power by actual throttle fraction / typical 0.15
        hoverWhPerMin = PowerModel::defaultHoverWhPerMin(airframe) * (hoverThrottle / 0.15);
    } else {
        hoverWhPerMin = PowerModel::defaultHoverWhPerMin(airframe);
    }

    // ── Energy calculation (single safety margin applied here) ──
    double cruiseWh = missionDistance * est.whPerKm;
    // 2 minutes hover for takeoff + landing
    double hoverWh = 2.0 * hoverWhPerMin;
    double requiredWh = cruiseWh + hoverWh;
    double safetyWh = batteryCapacityWh * m_batterySafetyFraction;
    double requiredPct = (requiredWh / batteryCapacityWh) * 100.0;

    QString throttleNote;
    if (haveHoverThrottle)
        throttleNote = QStringLiteral("MOT_THST_HOVER=%1").arg(hoverThrottle, 0, 'f', 3);

    if (requiredWh > safetyWh) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Mission requires %1 Wh (%2% of battery). "
                                 "Safety limit: %3% (%4 Wh). %5%6")
                      .arg(requiredWh, 0, 'f', 0)
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(m_batterySafetyFraction * 100, 0, 'f', 0)
                      .arg(safetyWh, 0, 'f', 0)
                      .arg(est.sourceLabel)
                      .arg(throttleNote.isEmpty() ? QString() : QStringLiteral(" · ") + throttleNote));
    } else if (requiredPct > 50.0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Mission uses %1% of battery (limit %2%). "
                                 "Est. range: %3 km. %4%5")
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(m_batterySafetyFraction * 100, 0, 'f', 0)
                      .arg(est.rangeKm, 0, 'f', 1)
                      .arg(est.sourceLabel)
                      .arg(throttleNote.isEmpty() ? QString() : QStringLiteral(" · ") + throttleNote));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Mission feasible: %1% of battery. "
                                 "Est. range: %2 km. %3%4")
                      .arg(requiredPct, 0, 'f', 1)
                      .arg(est.rangeKm, 0, 'f', 1)
                      .arg(est.sourceLabel)
                      .arg(throttleNote.isEmpty() ? QString() : QStringLiteral(" · ") + throttleNote));
    }
}

double MissionEnergyCheck::readBatteryCapacityWh() const
{
    if (!m_telemetry) return -1;

    double capacityMah = -1.0;
    if (isParamAvailable(QStringLiteral("BATT_CAPACITY"))) {
        capacityMah = getTelemetryDouble(QStringLiteral("BATT_CAPACITY"));
    } else if (m_telemetry->hasParameter(QStringLiteral("BATT_CAPACITY"))) {
        capacityMah = static_cast<double>(m_telemetry->parameterValue(QStringLiteral("BATT_CAPACITY")));
    }
    if (capacityMah > 0) {
        double voltage = m_telemetry->batteryVoltage();
        if (voltage > 0) {
            double cellCount = qRound(voltage / 4.2);
            return capacityMah * cellCount * 3.7 / 1000.0;
        }
        // Fallback: assume 6S (22.2V nominal) if voltage not available
        return capacityMah * 6.0 * 3.7 / 1000.0;
    }

    double voltage = m_telemetry->batteryVoltage();
    if (voltage > 0) {
        double cellCount = qRound(voltage / 4.2);
        double estimatedMah = 5000.0;
        return estimatedMah * cellCount * 3.7 / 1000.0;
    }
    return -1;
}

double MissionEnergyCheck::estimateMissionDistanceKm() const
{
    if (!m_telemetry) return -1;

    // Use total Haversine distance from TelemetryBridge if available
    double total = m_telemetry->missionTotalDistance();
    if (total > 0)
        return total / 1000.0;

    // Fallback: use first WP distance heuristic
    int count = m_telemetry->missionCount();
    if (count < 2) return -1;

    double firstWpDist = m_telemetry->missionFirstWpDistance();
    if (firstWpDist > 0) {
        double avgLeg = firstWpDist * 0.7;
        return (avgLeg * (count - 1)) / 1000.0;
    }

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
    return QStringLiteral("%1 waypoints (total %2 km), battery %3%")
        .arg(m_telemetry->missionCount())
        .arg(m_telemetry->missionTotalDistance() > 0
                 ? QString::number(m_telemetry->missionTotalDistance() / 1000.0, 'f', 2)
                 : QStringLiteral("?"))
        .arg(m_telemetry->batteryPercent());
}
