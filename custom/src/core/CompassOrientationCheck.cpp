#include "CompassOrientationCheck.h"

#include <QJsonObject>

#include "TelemetryBridge.h"

CompassOrientationCheck::CompassOrientationCheck(TelemetryBridge *telemetry,
                                                 QObject *parent)
    : AbstractCheck(QStringLiteral("nav.compass.orientation"),
                    QStringLiteral("Compass Orientation"),
                    CheckCategory::Navigation, CheckType::Auto, true, true, parent)
{
    m_telemetry = telemetry;
}

void CompassOrientationCheck::applyVehicleConfig(const QJsonObject &config)
{
    if (config.contains(QStringLiteral("expectedRotation"))) {
        m_expectedRotation = config.value(QStringLiteral("expectedRotation")).toInt(-1);
    }
}

// Pass if rotation matches vehicle profile or is default (0); otherwise prompt for confirmation.
void CompassOrientationCheck::evaluate()
{
    if (status() == CheckStatus::Passed) return;
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QString orientParam;
    double orientVal = -1;

    // PX4: CAL_MAG0_ROT, ArduPilot: COMPASS_ORIENT
    if (isParamAvailable(QStringLiteral("param_CAL_MAG0_ROT"))) {
        orientVal = getTelemetryDouble(QStringLiteral("param_CAL_MAG0_ROT"));
        orientParam = QStringLiteral("CAL_MAG0_ROT");
    } else if (isParamAvailable(QStringLiteral("param_COMPASS_ORIENT"))) {
        orientVal = getTelemetryDouble(QStringLiteral("param_COMPASS_ORIENT"));
        orientParam = QStringLiteral("COMPASS_ORIENT");
    }

    if (orientParam.isEmpty()) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No compass orientation parameter (CAL_MAG0_ROT or COMPASS_ORIENT)"));
        return;
    }

    int orient = static_cast<int>(orientVal);

    // Rotation code lookup (common values: 0=None, 1=Yaw45, 2=Yaw90, ...)
    static const char* rotationNames[] = {
        "None (0)", "Yaw45 (1)", "Yaw90 (2)", "Yaw135 (3)",
        "Yaw180 (4)", "Yaw225 (5)", "Yaw270 (6)", "Yaw315 (7)",
        "Roll180 (8)", "Roll180Yaw45 (9)", "Roll180Yaw90 (10)",
        "Roll180Yaw135 (11)", "Pitch180 (12)", "Roll180Yaw225 (13)",
        "Roll180Yaw270 (14)", "Roll180Yaw315 (15)", "Roll90 (16)",
        "Roll90Yaw45 (17)", "Roll90Yaw90 (18)", "Roll90Yaw135 (19)",
        "Roll270 (20)", "Roll270Yaw45 (21)", "Roll270Yaw90 (22)",
        "Roll270Yaw135 (23)", "Pitch90 (24)", "Pitch270 (25)",
        "Pitch180Yaw90 (26)", "Pitch180Yaw270 (27)", "Roll90Pitch90 (28)",
        "Roll180Pitch90 (29)", "Roll270Pitch90 (30)", "Roll90Pitch180 (31)",
        "Roll270Pitch180 (32)"
    };

    QString orientStr = (orient >= 0 && orient <= 32)
        ? QString::fromLatin1(rotationNames[orient])
        : QString::number(orient);

    setCurrentValue(QStringLiteral("%1=%2").arg(orientParam).arg(orientStr));

    // If vehicle config has an expected rotation and it matches, auto-pass
    if (m_expectedRotation >= 0 && orient == m_expectedRotation) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Compass orientation: %1 (%2) — matches vehicle profile")
                      .arg(orientParam).arg(orientStr));
        return;
    }

    if (orient == 0) {
        // Most airframes have compass at ROTATION_NONE — auto-pass
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Compass orientation: %1 (%2) — default orientation")
                      .arg(orientParam).arg(orientStr));
    } else {
        // Non-default orientation requires operator confirmation
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Confirm compass orientation: %1=%2")
                      .arg(orientParam).arg(orientStr));
    }
}
