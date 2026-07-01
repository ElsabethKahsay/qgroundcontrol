#include "LevelCalibrationCheck.h"

#include "TelemetryBridge.h"

LevelCalibrationCheck::LevelCalibrationCheck(TelemetryBridge *telemetry,
                                             double maxPitchRollDeg,
                                             QObject *parent)
    : AbstractCheck(QStringLiteral("nav.imu.level_calibration"),
                    QStringLiteral("Level Calibration"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxPitchRollDeg(maxPitchRollDeg)
{
    m_telemetry = telemetry;
}

void LevelCalibrationCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_AHRS_TRIM_X")) &&
        !isParamAvailable(QStringLiteral("param_AHRS_TRIM_Y"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("AHRS_TRIM params not available from vehicle"));
        return;
    }

    double pitchTrim = getTelemetryDouble(QStringLiteral("param_AHRS_TRIM_X"));
    double rollTrim = getTelemetryDouble(QStringLiteral("param_AHRS_TRIM_Y"));

    double pitchDeg = qAbs(pitchTrim);
    double rollDeg = qAbs(rollTrim);

    if (pitchDeg <= m_maxPitchRollDeg && rollDeg <= m_maxPitchRollDeg) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Pitch %1° Roll %2° — OK")
                      .arg(pitchDeg, 0, 'f', 1)
                      .arg(rollDeg, 0, 'f', 1));
        return;
    }

    if (pitchDeg > m_maxPitchRollDeg && rollDeg > m_maxPitchRollDeg) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Pitch %1° Roll %2° both exceed %3°")
                      .arg(pitchDeg, 0, 'f', 1)
                      .arg(rollDeg, 0, 'f', 1)
                      .arg(m_maxPitchRollDeg, 0, 'f', 1));
        return;
    }

    if (pitchDeg > m_maxPitchRollDeg) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Pitch offset %1° exceeds %2°")
                      .arg(pitchDeg, 0, 'f', 1)
                      .arg(m_maxPitchRollDeg, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("Roll offset %1° exceeds %2°")
                  .arg(rollDeg, 0, 'f', 1)
                  .arg(m_maxPitchRollDeg, 0, 'f', 1));
}
