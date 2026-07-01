#include "TelemetryDropRateCheck.h"

#include "TelemetryBridge.h"

TelemetryDropRateCheck::TelemetryDropRateCheck(TelemetryBridge *telemetry,
                                               uint maxDropRate,
                                               uint warnDropRate,
                                               QObject *parent)
    : AbstractCheck(QStringLiteral("com.telemetry.drop_rate"),
                    QStringLiteral("Telemetry Drop Rate"),
                    CheckCategory::Communication, CheckType::Auto, false, true, parent)
    , m_maxDropRate(maxDropRate)
    , m_warnDropRate(warnDropRate)
{
    m_telemetry = telemetry;
}

void TelemetryDropRateCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double dropRate = getTelemetryDouble(QStringLiteral("commDropRate"));

    if (dropRate > m_maxDropRate) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Drop rate %1% exceeds %2%")
                      .arg(static_cast<int>(dropRate)).arg(m_maxDropRate));
    } else if (dropRate > m_warnDropRate) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Drop rate %1% exceeds %2%")
                      .arg(static_cast<int>(dropRate)).arg(m_warnDropRate));
    } else {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Drop rate %1%").arg(static_cast<int>(dropRate)));
    }
}
