#include "EkfStatusFlagsCheck.h"

#include "TelemetryBridge.h"

EkfStatusFlagsCheck::EkfStatusFlagsCheck(TelemetryBridge *telemetry,
                                         uint requiredHealthFlags,
                                         double maxRatio,
                                         QObject *parent)
    : AbstractCheck(QStringLiteral("nav.ekf.status_flags"),
                    QStringLiteral("EKF Status Flags"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_requiredHealthFlags(requiredHealthFlags)
    , m_maxRatio(maxRatio)
{
    m_telemetry = telemetry;
}

void EkfStatusFlagsCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    uint flags = static_cast<uint>(getTelemetryDouble(QStringLiteral("estimatorFlags")));

    if (flags == 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("EKF not yet active"));
        return;
    }

    QStringList issues;

    // Fault flags must be clear
    if (flags & EstGpsGlitch) {
        issues << QStringLiteral("GPS glitch detected");
    }
    if (flags & EstAccelError) {
        issues << QStringLiteral("Accelerometer error");
    }

    // Required health flags must be set
    uint healthMissing = m_requiredHealthFlags & ~flags;
    if (healthMissing & EstAttitude) {
        issues << QStringLiteral("Attitude estimate invalid");
    }
    if (healthMissing & EstVelHoriz) {
        issues << QStringLiteral("Horizontal velocity estimate invalid");
    }
    if (healthMissing & EstVelVert) {
        issues << QStringLiteral("Vertical velocity estimate invalid");
    }
    if (healthMissing & EstPosHorizAbs) {
        issues << QStringLiteral("Absolute horizontal position estimate invalid");
    }
    if (healthMissing & EstPosVertAbs) {
        issues << QStringLiteral("Absolute vertical position estimate invalid");
    }

    // Innovation ratios
    double velRatio = getTelemetryDouble(QStringLiteral("estimatorVelRatio"));
    double posHorizRatio = getTelemetryDouble(QStringLiteral("estimatorPosHorizRatio"));
    double posVertRatio = getTelemetryDouble(QStringLiteral("estimatorPosVertRatio"));
    double magRatio = getTelemetryDouble(QStringLiteral("estimatorMagRatio"));

    if (velRatio >= m_maxRatio) {
        issues << QStringLiteral("VelRatio=%1").arg(velRatio, 0, 'f', 2);
    }
    if (posHorizRatio >= m_maxRatio) {
        issues << QStringLiteral("HorizRatio=%1").arg(posHorizRatio, 0, 'f', 2);
    }
    if (posVertRatio >= m_maxRatio) {
        issues << QStringLiteral("VertRatio=%1").arg(posVertRatio, 0, 'f', 2);
    }
    if (magRatio >= m_maxRatio) {
        issues << QStringLiteral("MagRatio=%1").arg(magRatio, 0, 'f', 2);
    }

    if (issues.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Flags 0x%1 Ratios OK").arg(flags, 0, 16));
    } else {
        setStatus(CheckStatus::Failed, issues.join(QStringLiteral(", ")));
    }
}
