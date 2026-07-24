#include "RedundantPowerCheck.h"

#include "TelemetryBridge.h"

RedundantPowerCheck::RedundantPowerCheck(TelemetryBridge *telemetry,
                                         double minVoltage, QObject *parent)
    : AbstractCheck(QStringLiteral("power.redundant"),
                    QStringLiteral("Redundant Power"),
                    CheckCategory::Power, CheckType::Auto, false, true, parent)
    , m_minVoltage(minVoltage)
{
    m_telemetry = telemetry;
}

// Pass if secondary battery present and voltage >= minVoltage; skip if absent; fail if low.
void RedundantPowerCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    bool present = getTelemetryBool(QStringLiteral("battery2Present"));
    double voltage = getTelemetryDouble(QStringLiteral("battery2Voltage"));
    int percent = static_cast<int>(getTelemetryDouble(QStringLiteral("battery2Percent")));

    if (!present || voltage <= 0.0) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No secondary battery detected"));
        return;
    }

    setCurrentValue(voltage);

    if (voltage >= m_minVoltage) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Secondary battery: %1 V (%2%) — OK")
                      .arg(voltage, 0, 'f', 2).arg(percent));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Secondary battery low: %1 V — below %2 V minimum")
                      .arg(voltage, 0, 'f', 2).arg(m_minVoltage, 0, 'f', 1));
    }
}

QString RedundantPowerCheck::getRationale() const
{
    return QStringLiteral("Redundant power is only useful if the secondary battery "
                          "is actually connected and charged. A depleted or missing "
                          "secondary battery provides no backup in the event of "
                          "primary power failure.");
}

QStringList RedundantPowerCheck::getFixSteps() const
{
    return {
        QStringLiteral("Connect or charge the secondary battery"),
        QStringLiteral("Verify the secondary battery connector is secure"),
        QStringLiteral("Check the secondary power module and wiring")
    };
}
