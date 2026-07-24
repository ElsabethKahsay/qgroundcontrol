#include "RcThrottleMinCheck.h"

#include "TelemetryBridge.h"

RcThrottleMinCheck::RcThrottleMinCheck(TelemetryBridge *telemetry,
                                       QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.throttle_min"),
                    QStringLiteral("RC Throttle Minimum"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when the throttle channel value >= 900 (stick fully down).
// Fails if throttle is above zero but below 900 (stick not at minimum); warns on zero signal.
void RcThrottleMinCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariantList channels = m_telemetry->property("rcChannelValues").toList();

    if (channels.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channel data"));
        return;
    }

    int throttleChan = isParamAvailable(QStringLiteral("param_RC_MAP_THROTTLE"))
        ? static_cast<int>(getTelemetryDouble(QStringLiteral("param_RC_MAP_THROTTLE")))
        : 3;
    int idx = throttleChan - 1;

    if (idx < 0 || idx >= channels.size()) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Throttle channel %1 not in range").arg(throttleChan));
        return;
    }

    uint raw = channels[idx].toUInt();

    if (raw >= 900) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Throttle channel %1 at %2").arg(throttleChan).arg(raw));
    } else if (raw > 0) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Throttle not at minimum (channel %1 = %2)")
                      .arg(throttleChan).arg(raw));
    } else {
        bool rcConnected = getTelemetryBool(QStringLiteral("rcConnected"));
        if (rcConnected) {
            setStatus(CheckStatus::Warning, QStringLiteral("No RC signal on throttle channel"));
        } else {
            setStatus(CheckStatus::Warning, QStringLiteral("RC not connected"));
        }
    }
}
