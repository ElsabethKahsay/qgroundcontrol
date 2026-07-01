#include "CellConfigCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

#include <algorithm>

CellConfigCheck::CellConfigCheck(TelemetryBridge *telemetry,
                                 double minCellVoltage,
                                 int maxCellDelta,
                                 QObject *parent)
    : AbstractCheck(QStringLiteral("power.battery.cell_config"),
                    QStringLiteral("Battery Cell Config"),
                    CheckCategory::Power, CheckType::Auto, true, false, parent)
    , m_minCellVoltage(minCellVoltage)
    , m_maxCellDelta(maxCellDelta)
{
    m_telemetry = telemetry;
}

void CellConfigCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariantList cells = m_telemetry->property("batteryCellVoltages").toList();

    if (cells.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No cell voltage data"));
        return;
    }

    // PX4: BATT_CELL_COUNT, ArduPilot: BAT_N_CELLS
    int expectedCount = 0;
    if (isParamAvailable(QStringLiteral("param_BATT_CELL_COUNT"))) {
        expectedCount = static_cast<int>(getTelemetryDouble(QStringLiteral("param_BATT_CELL_COUNT")));
    } else if (isParamAvailable(QStringLiteral("param_BAT_N_CELLS"))) {
        expectedCount = static_cast<int>(getTelemetryDouble(QStringLiteral("param_BAT_N_CELLS")));
    } else {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No cell count param (BATT_CELL_COUNT or BAT_N_CELLS)"));
        return;
    }

    // Per SRS POW-002: also estimate cell count from total voltage / 4.2
    double totalVoltage = getTelemetryDouble(QStringLiteral("batteryVoltage"));
    int detectedCells = 0;
    if (totalVoltage > 0.0) {
        detectedCells = static_cast<int>(qRound(totalVoltage / 4.2));
    }

    int actualCount = cells.size();
    int delta = qAbs(actualCount - expectedCount);

    if (delta > m_maxCellDelta) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Expected %1 cells, got %2 (delta %3)")
                      .arg(expectedCount).arg(actualCount).arg(delta));
        return;
    }

    // Warn if detected cells (from total voltage) differs from configured
    if (detectedCells > 0 && qAbs(detectedCells - expectedCount) > 1) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Voltage suggests %1S battery but configured for %2S")
                      .arg(detectedCells).arg(expectedCount));
        return;
    }

    for (int i = 0; i < actualCount; ++i) {
        double v = cells[i].toDouble();
        if (v < m_minCellVoltage) {
            setStatus(CheckStatus::Failed,
                      QStringLiteral("Cell %1 voltage %2V below minimum %3V")
                          .arg(i + 1).arg(v, 0, 'f', 2).arg(m_minCellVoltage, 0, 'f', 1));
            return;
        }
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("%1 cells OK").arg(actualCount));
}

QString CellConfigCheck::getRationale() const
{
    return QStringLiteral("Battery cell count must match the configured parameter value. A mismatch "
                          "indicates the wrong battery type is connected, which can cause improper "
                          "voltage monitoring and premature or delayed arm/disarm decisions.");
}

QStringList CellConfigCheck::getFixSteps() const
{
    return {
        QStringLiteral("Verify the connected battery matches the expected cell count"),
        QStringLiteral("Update BATT_CELL_COUNT (PX4) or BAT_N_CELLS (ArduPilot) to match the battery"),
        QStringLiteral("Check for damaged balance leads or broken cell monitoring connections"),
        QStringLiteral("Replace the battery with one matching the configured cell count")
    };
}

QString CellConfigCheck::getThreshold() const
{
    return QStringLiteral("All cells ≥%1V, cell count matches param, delta ≤%2")
        .arg(m_minCellVoltage, 0, 'f', 1).arg(m_maxCellDelta);
}

QString CellConfigCheck::getCurrentValueString() const
{
    QVariantList cells = m_telemetry->property("batteryCellVoltages").toList();
    if (cells.isEmpty())
        return QStringLiteral("No data");
    QVector<double> vs;
    for (const auto &v : cells)
        vs.append(v.toDouble());
    std::sort(vs.begin(), vs.end());
    return QStringLiteral("%1 cells, %2V–%3V")
        .arg(cells.size())
        .arg(vs.first(), 0, 'f', 2)
        .arg(vs.last(), 0, 'f', 2);
}
