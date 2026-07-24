#include "TakeoffCommandCheck.h"

#include "MissionManager/MissionItem.h"
#include "MissionManager/MissionManager.h"
#include "Vehicle/Vehicle.h"

#include "TelemetryBridge.h"

TakeoffCommandCheck::TakeoffCommandCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("mission.takeoff.command"),
                    QStringLiteral("Takeoff Command"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when a MAV_CMD_NAV_TAKEOFF item exists in the mission (reports its altitude).
// Warns if no takeoff command is found, indicating RTL may use the default ascent behavior.
void TakeoffCommandCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int count = static_cast<int>(getTelemetryDouble(QStringLiteral("missionCount")));
    if (count <= 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No mission loaded"));
        return;
    }

    Vehicle *v = m_telemetry->vehicle();
    if (!v) {
        setStatus(CheckStatus::Pending, QStringLiteral("No vehicle"));
        return;
    }
    auto *mgr = v->missionManager();
    if (!mgr) {
        setStatus(CheckStatus::Pending, QStringLiteral("No mission manager"));
        return;
    }

    const QList<MissionItem*> &items = mgr->missionItems();
    bool hasTakeoff = false;
    double takeoffAlt = 0.0;
    for (const auto *item : items) {
        if (item->command() == MAV_CMD_NAV_TAKEOFF) {
            hasTakeoff = true;
            takeoffAlt = item->param7();
            break;
        }
    }

    if (hasTakeoff) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("MAV_CMD_NAV_TAKEOFF found, altitude %1 m")
                      .arg(takeoffAlt, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("No MAV_CMD_NAV_TAKEOFF in mission — RTL may ascend at default rate"));
    }
}
