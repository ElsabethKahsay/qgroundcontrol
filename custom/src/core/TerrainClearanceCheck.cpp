#include "TerrainClearanceCheck.h"

#include "TelemetryBridge.h"

TerrainClearanceCheck::TerrainClearanceCheck(TelemetryBridge *telemetry, double minTerrainClearance, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.terrain.clearance"),
                    QStringLiteral("Terrain Clearance"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
    , m_minTerrainClearance(minTerrainClearance)
{
    m_telemetry = telemetry;
}

void TerrainClearanceCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double terrainHeight = getTelemetryDouble(QStringLiteral("terrainHeight"));

    if (qIsNaN(terrainHeight)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No TERRAIN_REPORT data available"));
        return;
    }

    setCurrentValue(terrainHeight);

    if (terrainHeight < m_minTerrainClearance) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Terrain clearance %1 m — below %2 m threshold")
                      .arg(terrainHeight, 0, 'f', 1).arg(m_minTerrainClearance, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Terrain clearance %1 m").arg(terrainHeight, 0, 'f', 1));
}
