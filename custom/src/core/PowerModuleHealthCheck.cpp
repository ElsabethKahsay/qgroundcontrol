#include "PowerModuleHealthCheck.h"

#include "TelemetryBridge.h"

PowerModuleHealthCheck::PowerModuleHealthCheck(TelemetryBridge *telemetry,
                                               double maxDeltaV, QObject *parent)
    : AbstractCheck(QStringLiteral("power.module.health"),
                    QStringLiteral("Power Module Health"),
                    CheckCategory::Power, CheckType::Auto, false, true, parent)
    , m_maxDeltaV(maxDeltaV)
{
    m_telemetry = telemetry;
}

// Warn if |sysVoltage - battVoltage| exceeds maxDeltaV; pass if the two readings agree.
void PowerModuleHealthCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double sysVoltage = getTelemetryDouble(QStringLiteral("sysVoltageBattery"));
    double battVoltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));

    if (sysVoltage <= 0.0 && battVoltage <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No voltage data"));
        return;
    }

    if (sysVoltage <= 0.0) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No SYS_STATUS voltage data"));
        return;
    }

    if (battVoltage <= 0.0) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No BATTERY_STATUS voltage data"));
        return;
    }

    double delta = qAbs(sysVoltage - battVoltage);
    setCurrentValue(delta);

    if (delta <= m_maxDeltaV) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("PM %1 V / Battery %2 V — Δ%3 V — OK")
                      .arg(sysVoltage, 0, 'f', 2)
                      .arg(battVoltage, 0, 'f', 2)
                      .arg(delta, 0, 'f', 2));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Power module mismatch: PM %1 V vs battery %2 V (Δ%3 V)")
                      .arg(sysVoltage, 0, 'f', 2)
                      .arg(battVoltage, 0, 'f', 2)
                      .arg(delta, 0, 'f', 2));
    }
}

QString PowerModuleHealthCheck::getRationale() const
{
    return QStringLiteral("The power module voltage should match the battery voltage. "
                          "A large delta indicates a failing power module, incorrect "
                          "voltage multiplier, or wiring issue.");
}

QStringList PowerModuleHealthCheck::getFixSteps() const
{
    return {
        QStringLiteral("Recalibrate the power module (BATT_VOLT_MULT parameter)"),
        QStringLiteral("Check power module wiring and solder joints"),
        QStringLiteral("Replace the power module if the delta persists after calibration")
    };
}

QString PowerModuleHealthCheck::getThreshold() const
{
    return QStringLiteral("Max Δ%1 V").arg(m_maxDeltaV, 0, 'f', 1);
}
