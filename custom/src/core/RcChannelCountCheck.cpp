#include "RcChannelCountCheck.h"

#include "TelemetryBridge.h"

RcChannelCountCheck::RcChannelCountCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("comm.rc.channel_count"),
                    QStringLiteral("RC Channel Count"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

void RcChannelCountCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariantList channels = getTelemetryVariant(QStringLiteral("rcChannelValues")).toList();
    int chanCount = channels.size();
    setCurrentValue(chanCount);

    if (chanCount == 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channels received"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_RC_CHAN_CNT"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("RC_CHAN_CNT not available from vehicle"));
        return;
    }

    double paramCnt = getTelemetryDouble(QStringLiteral("param_RC_CHAN_CNT"));

    int expected = static_cast<int>(paramCnt);
    if (chanCount >= expected) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 channels (expected %2) — OK")
                      .arg(chanCount).arg(expected));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("%1 channels — expected %2 per RC_CHAN_CNT")
                      .arg(chanCount).arg(expected));
    }
}
