#include "RcCheck.h"

#include "RcArmingSwitchCheck.h"
#include "RcCalibrationCheck.h"
#include "RcChannelCountCheck.h"
#include "RcThrottleMinCheck.h"
#include "RcTrimCheck.h"
#include "TelemetryBridge.h"

namespace {

// Ordering used to surface the most severe sub-check status.
int statusRank(CheckStatus s)
{
    switch (s) {
    case CheckStatus::Error:
    case CheckStatus::Failed:  return 4;
    case CheckStatus::Warning: return 3;
    case CheckStatus::Pending:
    case CheckStatus::Stale:   return 2;
    case CheckStatus::Skipped: return 1;
    case CheckStatus::Passed:  return 0;
    }
    return 0;
}

} // namespace

RcCheck::RcCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc"),
                    QStringLiteral("RC Check"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;

    // The sub-checks keep their original evaluation logic; only their results
    // are rolled up into this single check.
    m_subChecks = {
        new RcArmingSwitchCheck(telemetry, this),
        new RcCalibrationCheck(telemetry, this),
        new RcThrottleMinCheck(telemetry, this),
        new RcTrimCheck(telemetry, 50, this),
        new RcChannelCountCheck(telemetry, this),
    };
}

void RcCheck::evaluate()
{
    CheckStatus worst = CheckStatus::Passed;
    QStringList issues;
    int passed = 0;
    bool anyEvaluated = false;

    for (AbstractCheck *sub : m_subChecks) {
        sub->evaluate();
        const CheckStatus s = sub->status();

        if (s != CheckStatus::Pending)
            anyEvaluated = true;

        if (s == CheckStatus::Passed) {
            ++passed;
            continue;
        }

        if (s == CheckStatus::Failed || s == CheckStatus::Error ||
            s == CheckStatus::Warning) {
            issues << QStringLiteral("%1: %2").arg(sub->label(), sub->message());
        }

        if (statusRank(s) > statusRank(worst))
            worst = s;
    }

    if (!anyEvaluated) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for RC telemetry"));
        return;
    }

    if (worst == CheckStatus::Passed) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("All RC checks passed (%1/%2)")
                      .arg(passed).arg(m_subChecks.size()));
    } else if (worst == CheckStatus::Skipped) {
        setStatus(CheckStatus::Skipped, QStringLiteral("RC checks skipped"));
    } else if (worst == CheckStatus::Pending || worst == CheckStatus::Stale) {
        setStatus(worst, QStringLiteral("Waiting for RC telemetry"));
    } else {
        setStatus(worst, issues.join(QStringLiteral("; ")));
    }
}

QString RcCheck::getRationale() const
{
    return QStringLiteral("Combined RC transmitter check covering arming switch "
                          "configuration, calibration, throttle minimum, trim "
                          "centering and channel count.");
}

QStringList RcCheck::getFixSteps() const
{
    return {
        QStringLiteral("Run RC calibration in your autopilot firmware"),
        QStringLiteral("Center transmitter trims (Roll/Pitch/Yaw near 1500 \u00b5s)"),
        QStringLiteral("Lower the throttle stick fully before arming"),
        QStringLiteral("Verify RC_CHAN_CNT matches the receiver channel count"),
        QStringLiteral("Enable RC arming (ARMING_RC_ENABLE=1) if required"),
    };
}

QString RcCheck::getCurrentValueString() const
{
    if (!hasTelemetry())
        return QStringLiteral("No telemetry");

    int passed = 0;
    for (AbstractCheck *sub : m_subChecks) {
        if (sub->status() == CheckStatus::Passed)
            ++passed;
    }
    return QStringLiteral("%1/%2 OK").arg(passed).arg(m_subChecks.size());
}

QString RcCheck::getThreshold() const
{
    return QStringLiteral("Throttle \u2265 900 \u00b5s; \u2265 RC_CHAN_CNT channels; "
                          "trims within \u00b150 \u00b5s of 1500; endpoints within calibrated range");
}
