#include "VibrationFailsafeCheck.h"

#include "TelemetryBridge.h"

VibrationFailsafeCheck::VibrationFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.vibration_failsafe"),
                    QStringLiteral("Vibration Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void VibrationFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_VIBE_ACTION"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("VIBE_ACTION not available from vehicle"));
        return;
    }

    double vibeAction = getTelemetryDouble(QStringLiteral("param_VIBE_ACTION"));

    int action = static_cast<int>(vibeAction);

    if (action >= 1) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("VIBE_ACTION=%1 — failsafe enabled").arg(action));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("VIBE_ACTION=0 — vibration failsafe disabled"));
}
