#include "CurrentSensorCheck.h"

#include "TelemetryBridge.h"

CurrentSensorCheck::CurrentSensorCheck(TelemetryBridge *telemetry,
                                       double maxDisarmedCurrent,
                                       double minArmedCurrent,
                                       QObject *parent)
    : AbstractCheck(QStringLiteral("power.current.sensor"),
                    QStringLiteral("Current Sensor Sanity"),
                    CheckCategory::Power, CheckType::Auto, false, false, parent)
    , m_maxDisarmedCurrent(maxDisarmedCurrent)
    , m_minArmedCurrent(minArmedCurrent)
{
    m_telemetry = telemetry;
}

void CurrentSensorCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double current = getTelemetryDouble(QStringLiteral("batteryCurrent"));
    double voltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));
    bool isArmed = getTelemetryBool(QStringLiteral("armed"));

    if (voltage <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No battery data"));
        return;
    }

    // Check for stuck sensor (exactly 0.0 when voltage is present)
    bool sensorStuck = (qFuzzyIsNull(current) && voltage > 0.0);

    setCurrentValue(current);

    if (isArmed) {
        if (sensorStuck) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Current sensor reporting 0A while armed — possible sensor fault"));
            return;
        }
        if (current < 0.0) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Negative current %1 A while armed — sensor may be inverted")
                          .arg(current, 0, 'f', 1));
            return;
        }
        if (current < m_minArmedCurrent) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Low current %1 A while armed — check sensor calibration")
                          .arg(current, 0, 'f', 1));
            return;
        }
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 A — OK").arg(current, 0, 'f', 1));
    } else {
        if (sensorStuck) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Current sensor reporting 0A — verify calibration"));
            return;
        }
        if (current > m_maxDisarmedCurrent) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Current %1 A while disarmed — possible power drain or sensor offset")
                          .arg(current, 0, 'f', 1));
            return;
        }
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 A — OK (disarmed)").arg(current, 0, 'f', 1));
    }
}
