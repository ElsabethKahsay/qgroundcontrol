#include "BatteryVoltageCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

BatteryVoltageCheck::BatteryVoltageCheck(TelemetryBridge *telemetry,
                                         double minVoltage,
                                         double minPercent,
                                         QObject *parent)
    : AbstractCheck(QStringLiteral("power.battery.voltage"),
                    QStringLiteral("Battery Voltage"),
                    CheckCategory::Power, CheckType::Auto, true, false, parent)
    , m_minVoltage(minVoltage)
    , m_minPercent(minPercent)
{
    m_telemetry = telemetry;
}

double BatteryVoltageCheck::effectiveMinVoltage() const
{
    // PX4: cell-aware via BAT_V_EMPTY (per-cell empty voltage)
    if (isParamAvailable(QStringLiteral("param_BAT_V_EMPTY")) &&
        (isParamAvailable(QStringLiteral("param_BAT_CELL_COUNT")) ||
         isParamAvailable(QStringLiteral("param_BATT_CELL_COUNT")))) {
        double vEmpty = getTelemetryDouble(QStringLiteral("param_BAT_V_EMPTY"));
        int cells = effectiveCellCount();
        if (vEmpty > 0.0 && cells > 0)
            return cells * vEmpty;
    }

    // ArduPilot: BATT_LOW_VOLT is total low-voltage threshold
    if (isParamAvailable(QStringLiteral("param_BATT_LOW_VOLT"))) {
        double battLow = getTelemetryDouble(QStringLiteral("param_BATT_LOW_VOLT"));
        if (battLow > 0.0)
            return battLow;
    }

    // Cell-aware fallback: cells × 3.3V per cell
    double cellCount = effectiveCellCount();
    if (cellCount > 0)
        return qMax(cellCount * 3.3, 10.0);

    if (m_minVoltage > 0.0)
        return m_minVoltage;
    return 15.0;
}

// Pass if voltage >= min threshold. Warn if low on capacity but voltage still ok.
// Fail if voltage is below threshold and capacity is also insufficient.
void BatteryVoltageCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double voltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));
    double current = getTelemetryDouble(QStringLiteral("batteryCurrentAmps"));
    if (current <= 0.0) {
        current = getTelemetryDouble(QStringLiteral("batteryCurrent"));
    }
    int percent = static_cast<int>(getTelemetryDouble(QStringLiteral("batteryPercent")));
    QString chargeState = getTelemetryVariant(QStringLiteral("batteryChargeState")).toString();
    int cells = effectiveCellCount();

    setCurrentValue(voltage);

    if (voltage <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No battery data"));
        return;
    }

    double minV = effectiveMinVoltage();
    bool voltageOk = voltage >= minV;
    double minPct = effectiveMinPercent();
    bool percentOk = percent >= minPct || (percent == 0 && voltage >= minV);
    bool chargeOk = chargeState.isEmpty() || chargeState == QStringLiteral("OK");

    QString msg;
    if (voltageOk && chargeOk) {
        msg = QStringLiteral("%1V %2S %3A — OK")
            .arg(voltage, 0, 'f', 1).arg(cells).arg(current, 0, 'f', 1);
        setStatus(CheckStatus::Passed, msg);
    } else if (!chargeOk && (chargeState == QStringLiteral("critical") || chargeState == QStringLiteral("emergency"))) {
        msg = QStringLiteral("%1V %2S — Battery state: %3")
            .arg(voltage, 0, 'f', 1).arg(cells).arg(chargeState);
        setStatus(CheckStatus::Failed, msg);
    } else if (percentOk) {
        msg = QStringLiteral("%1V %2S — %3%, low voltage")
            .arg(voltage, 0, 'f', 1).arg(cells).arg(percent);
        setStatus(CheckStatus::Warning, msg);
    } else {
        msg = QStringLiteral("%1V %2S — below %3V threshold")
            .arg(voltage, 0, 'f', 1).arg(cells).arg(minV, 0, 'f', 1);
        setStatus(CheckStatus::Failed, msg);
    }
}

int BatteryVoltageCheck::effectiveCellCount() const
{
    double c = getTelemetryDouble(QStringLiteral("param_BAT_CELL_COUNT"));
    if (c <= 0.0)
        c = getTelemetryDouble(QStringLiteral("param_BATT_CELL_COUNT"));
    if (c > 0.0)
        return qBound(1, static_cast<int>(c), 16);
    return static_cast<int>(estimateCellCount());
}

double BatteryVoltageCheck::effectiveMinPercent() const
{
    double pct = getTelemetryDouble(QStringLiteral("param_BAT_LOW_THR"));
    if (pct > 0.0)
        return qBound(1.0, pct, 50.0);
    return m_minPercent;
}

double BatteryVoltageCheck::estimateCellCount() const
{
    double v = getTelemetryDouble(QStringLiteral("batteryVoltage"));
    if (v <= 0.0) return 0;
    // Use nominal voltage (3.7V/cell) to avoid undercounting on discharged packs
    double cells = qRound(v / 3.7);
    return qBound(1.0, cells, 16.0);
}

QString BatteryVoltageCheck::getRationale() const
{
    return QStringLiteral("Adequate battery voltage is required to ensure sufficient power for "
                          "motors, avionics, and payload throughout the planned flight duration.");
}

QStringList BatteryVoltageCheck::getFixSteps() const
{
    return {
        QStringLiteral("Charge the battery to full capacity before flight"),
        QStringLiteral("Replace the battery if it no longer holds a charge"),
        QStringLiteral("Check battery connectors for secure attachment"),
        QStringLiteral("Verify BATT_CELL_COUNT and BATT_LOW_VOLT parameters match the battery")
    };
}

QString BatteryVoltageCheck::getThreshold() const
{
    double minV = effectiveMinVoltage();
    double minPct = effectiveMinPercent();
    return QStringLiteral("Min %1V or %2% remaining").arg(minV, 0, 'f', 1).arg(minPct);
}

QString BatteryVoltageCheck::getCurrentValueString() const
{
    double voltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));
    int percent = static_cast<int>(getTelemetryDouble(QStringLiteral("batteryPercent")));
    int cells = effectiveCellCount();
    if (voltage <= 0.0)
        return QStringLiteral("No data");
    return QStringLiteral("%1V %2S %3%").arg(voltage, 0, 'f', 1).arg(cells).arg(percent);
}
