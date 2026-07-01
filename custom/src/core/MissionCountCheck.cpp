#include "MissionCountCheck.h"

#include "TelemetryBridge.h"

MissionCountCheck::MissionCountCheck(TelemetryBridge *telemetry, int minMissionCount, QObject *parent)
    : AbstractCheck(QStringLiteral("mission.count"),
                    QStringLiteral("Mission Uploaded"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_minMissionCount(minMissionCount)
{
    m_telemetry = telemetry;
}

void MissionCountCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int count = static_cast<int>(getTelemetryDouble(QStringLiteral("missionCount")));
    setCurrentValue(count);

    if (count <= 0) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("No mission loaded yet"));
        return;
    }

    if (count < m_minMissionCount) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Mission has %1 item(s) — need at least %2").arg(count).arg(m_minMissionCount));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("%1 mission item(s) loaded").arg(count));
}
