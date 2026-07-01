#include "CellVoltageBalanceCheck.h"

#include "TelemetryBridge.h"

CellVoltageBalanceCheck::CellVoltageBalanceCheck(TelemetryBridge *telemetry,
                                                 double maxDelta,
                                                 QObject *parent)
    : AbstractCheck(QStringLiteral("power.battery.cell_balance"),
                    QStringLiteral("Cell Voltage Balance"),
                    CheckCategory::Power, CheckType::Auto, true, false, parent)
    , m_maxDelta(maxDelta)
{
    m_telemetry = telemetry;
}

void CellVoltageBalanceCheck::evaluate()
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

    if (cells.size() < 2) {
        setStatus(CheckStatus::Pending, QStringLiteral("Need at least 2 cells"));
        return;
    }

    double minV = cells[0].toDouble();
    double maxV = cells[0].toDouble();

    for (int i = 1; i < cells.size(); ++i) {
        double v = cells[i].toDouble();
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    double delta = maxV - minV;

    // 5-second moving average for hysteresis
    m_deltaAvgFilter.addSample(delta);
    double avgDelta = m_deltaAvgFilter.average();

    setCurrentValue(avgDelta);

    if (avgDelta <= m_maxDelta) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Delta %1 V (%2 cells, avg %3s)")
                      .arg(avgDelta, 0, 'f', 3)
                      .arg(cells.size())
                      .arg(5));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Delta %1 V exceeds max %2 V (avg %3s)")
                      .arg(avgDelta, 0, 'f', 3)
                      .arg(m_maxDelta, 0, 'f', 2)
                      .arg(5));
    }
}
