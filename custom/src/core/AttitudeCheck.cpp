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

    if (qIsNaN(roll) || qIsNaN(pitch) || qIsNaN(yaw)) {
        setStatus(CheckStatus::Pending, QStringLiteral("No attitude data"));
        return;
    }

    double rollDeg  = roll * 180.0 / M_PI;
    double pitchDeg = pitch * 180.0 / M_PI;
    double yawDeg   = yaw * 180.0 / M_PI;

    setCurrentValue(QStringLiteral("R%1\u00B0 P%2\u00B0 Y%3\u00B0")
                        .arg(rollDeg, 0, 'f', 0)
                        .arg(pitchDeg, 0, 'f', 0)
                        .arg(yawDeg, 0, 'f', 0));

    // Pass if roll and pitch are within limits; fail if either exceeds the max tilt angle.
    // Two different problems look the same here: the airframe really is not
    // level, or it is level and the flight controller has the wrong idea of
    // "level" because it does not know how the board is mounted
    // (AHRS_ORIENTATION) or its accelerometers were calibrated badly. The
    // message names both so the operator checks the airframe first and the
    // setup second, rather than assuming the reading is right.
    if (std::abs(rollDeg) > m_maxPitchRollDeg || std::abs(pitchDeg) > m_maxPitchRollDeg) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Vehicle tilted: roll %1\u00B0 pitch %2\u00B0 \u2014 exceed %3\u00B0 limit. "
                                 "If it is sitting level, the flight controller's orientation (AHRS_ORIENTATION) "
                                 "or accelerometer calibration is wrong \u2014 QGC Vehicle Setup \u2192 Sensors")
                      .arg(rollDeg, 0, 'f', 1).arg(pitchDeg, 0, 'f', 1).arg(m_maxPitchRollDeg, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Roll %1\u00B0 Pitch %2\u00B0 Yaw %3\u00B0")
                  .arg(rollDeg, 0, 'f', 1).arg(pitchDeg, 0, 'f', 1).arg(yawDeg, 0, 'f', 1));
}

QStringList AttitudeCheck::getFixSteps() const
{
    return {
        QStringLiteral("Put the vehicle on a flat surface, wings / arms level, and look at the reading again."),
        QStringLiteral("If it still shows a large roll or pitch while level: in QGC open Vehicle Setup \u2192 Sensors "
                       "and set the flight controller orientation to how the board is mounted (the arrow on the "
                       "board against the nose of the vehicle). This is the AHRS_ORIENTATION parameter."),
        QStringLiteral("Then run the Accelerometer calibration on the same page and follow its positions."),
        QStringLiteral("Check again: roll and pitch must read close to 0\u00B0 when level. Do not fly until they do."),
    };
}
