#include "EscVoltageConsistencyCheck.h"

#include "TelemetryBridge.h"

EscVoltageConsistencyCheck::EscVoltageConsistencyCheck(TelemetryBridge *telemetry,
                                                       double maxDeltaV, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.esc.voltage_consistency"),
                    QStringLiteral("ESC Voltage Consistency"),
                    CheckCategory::Propulsion, CheckType::Auto, false, true, parent)
    , m_maxDeltaV(maxDeltaV)
{
    m_telemetry = telemetry;
}

// Pass: all ESC voltages within maxDeltaV (default 0.5 V) of battery voltage.
// Fail: any ESC voltage mismatch exceeds threshold.
void EscVoltageConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant voltsVar = getTelemetryVariant(QStringLiteral("escVoltages"));
    double batteryVoltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));

    if (!voltsVar.isValid() || voltsVar.toList().isEmpty()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No ESC telemetry"));
        return;
    }

    QVariantList volts = voltsVar.toList();

    if (batteryVoltage <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for battery voltage"));
        return;
    }

    QStringList mismatches;
    for (int i = 0; i < qMin(volts.size(), 4); ++i) {
        double v = volts[i].toDouble();
        double delta = qAbs(v - batteryVoltage);
        if (delta > m_maxDeltaV)
            mismatches.append(QStringLiteral("ESC%1=%2V vs %3V (Δ%4V)")
                                  .arg(i + 1).arg(v, 0, 'f', 2)
                                  .arg(batteryVoltage, 0, 'f', 2)
                                  .arg(delta, 0, 'f', 2));
    }

    if (mismatches.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("All ESC voltages match battery (%1 V)")
                      .arg(batteryVoltage, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Voltage mismatch: %1").arg(mismatches.join(QStringLiteral("; "))));
    }
}
