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

// Passes when the throttle stick is at its low end: within the bottom 10 % of
// the channel's calibrated range (RCn_MIN..RCn_MAX, at least 50 µs), honouring
// a reversed channel. Fails when the stick is above that; warns on no signal.
// (It used to pass for any value >= 900 µs, i.e. at any stick position.)
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

    int throttleChan = 3;   // ArduPilot RCMAP_THROTTLE / PX4 RC_MAP_THROTTLE default
    if (isParamAvailable(QStringLiteral("param_RCMAP_THROTTLE")))
        throttleChan = static_cast<int>(getTelemetryDouble(QStringLiteral("param_RCMAP_THROTTLE")));
    else if (isParamAvailable(QStringLiteral("param_RC_MAP_THROTTLE")))
        throttleChan = static_cast<int>(getTelemetryDouble(QStringLiteral("param_RC_MAP_THROTTLE")));
    int idx = throttleChan - 1;

    if (idx < 0 || idx >= channels.size()) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Throttle channel %1 not in range").arg(throttleChan));
        return;
    }

    uint raw = channels[idx].toUInt();

    if (raw > 0) {
        const QString p = QStringLiteral("param_RC%1_").arg(throttleChan);
        const double minPwm = isParamAvailable(p + QStringLiteral("MIN")) ? getTelemetryDouble(p + QStringLiteral("MIN")) : 1100.0;
        const double maxPwm = isParamAvailable(p + QStringLiteral("MAX")) ? getTelemetryDouble(p + QStringLiteral("MAX")) : 1900.0;
        // ArduPilot RCn_REVERSED = 1; PX4 RCn_REV = -1.
        const bool reversed =
            (isParamAvailable(p + QStringLiteral("REVERSED")) && getTelemetryDouble(p + QStringLiteral("REVERSED")) >= 1.0)
            || (isParamAvailable(p + QStringLiteral("REV")) && getTelemetryDouble(p + QStringLiteral("REV")) < 0.0);
        const double band = qMax(50.0, 0.10 * (maxPwm - minPwm));
        const bool atMinimum = reversed ? (raw >= maxPwm - band) : (raw <= minPwm + band);
        if (atMinimum) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("Throttle at minimum (channel %1 = %2 \u00b5s)").arg(throttleChan).arg(raw));
        } else {
            setStatus(CheckStatus::Failed,
                      QStringLiteral("Throttle not at minimum (channel %1 = %2 \u00b5s) \u2014 lower the throttle stick")
                          .arg(throttleChan).arg(raw));
        }
    } else {
        bool rcConnected = getTelemetryBool(QStringLiteral("rcConnected"));
        if (rcConnected) {
            setStatus(CheckStatus::Warning, QStringLiteral("No RC signal on throttle channel"));
        } else {
            setStatus(CheckStatus::Warning, QStringLiteral("RC not connected"));
        }
    }
}
