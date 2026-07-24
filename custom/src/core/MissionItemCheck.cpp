#include "MissionItemCheck.h"

#include "TelemetryBridge.h"

MissionItemCheck::MissionItemCheck(TelemetryBridge *telemetry, double maxFirstWpDistM, QObject *parent)
    : AbstractCheck(QStringLiteral("mission.item.validation"),
                    QStringLiteral("Mission Item Validation"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxFirstWpDistM(maxFirstWpDistM)
{
    m_telemetry = telemetry;
}

// Passes when the first waypoint distance is <= m_maxFirstWpDistM from home.
// Warns if it exceeds the limit; stays pending if no mission is loaded or distance is not yet available.
void MissionItemCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int count = static_cast<int>(getTelemetryDouble(QStringLiteral("missionCount")));
    double firstWpDist = getTelemetryDouble(QStringLiteral("missionFirstWpDistance"));

    setCurrentValue(count);

    if (count <= 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No mission loaded"));
        return;
    }

    if (firstWpDist < 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for first waypoint distance"));
        return;
    }

    if (firstWpDist > m_maxFirstWpDistM) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("First waypoint is %1 m from home — exceeds %2 m limit")
                      .arg(firstWpDist, 0, 'f', 0).arg(m_maxFirstWpDistM, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("First waypoint %1 m from home").arg(firstWpDist, 0, 'f', 0));
}
