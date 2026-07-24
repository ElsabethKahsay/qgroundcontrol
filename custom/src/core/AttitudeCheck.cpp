#include "AttitudeCheck.h"

#include <cmath>

#include "TelemetryBridge.h"

AttitudeCheck::AttitudeCheck(TelemetryBridge *telemetry, double maxPitchRollDeg, QObject *parent)
    : AbstractCheck(QStringLiteral("nav.attitude"),
                    QStringLiteral("Attitude"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxPitchRollDeg(maxPitchRollDeg)
{
    m_telemetry = telemetry;
}

void AttitudeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double roll  = getTelemetryDouble(QStringLiteral("roll"));
    double pitch = getTelemetryDouble(QStringLiteral("pitch"));
    double yaw   = getTelemetryDouble(QStringLiteral("yaw"));

    double rollDeg  = roll * 180.0 / M_PI;
    double pitchDeg = pitch * 180.0 / M_PI;
    double yawDeg   = yaw * 180.0 / M_PI;

    setCurrentValue(QStringLiteral("R%1\u00B0 P%2\u00B0 Y%3\u00B0")
                        .arg(rollDeg, 0, 'f', 0)
                        .arg(pitchDeg, 0, 'f', 0)
                        .arg(yawDeg, 0, 'f', 0));

    // Pass if roll and pitch are within limits; fail if either exceeds the max tilt angle.
    if (std::abs(rollDeg) > m_maxPitchRollDeg || std::abs(pitchDeg) > m_maxPitchRollDeg) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Vehicle tilted: roll %1\u00B0 pitch %2\u00B0 \u2014 exceed %3\u00B0 limit")
                      .arg(rollDeg, 0, 'f', 1).arg(pitchDeg, 0, 'f', 1).arg(m_maxPitchRollDeg, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Roll %1\u00B0 Pitch %2\u00B0 Yaw %3\u00B0")
                  .arg(rollDeg, 0, 'f', 1).arg(pitchDeg, 0, 'f', 1).arg(yawDeg, 0, 'f', 1));
}
