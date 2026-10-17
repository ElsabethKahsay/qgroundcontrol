#include "RcCheck.h"

#include "RcArmingSwitchCheck.h"
#include "RcCalibrationCheck.h"
#include "RcChannelCountCheck.h"
#include "RcThrottleMinCheck.h"
#include "RcTrimCheck.h"
#include "TelemetryBridge.h"

namespace {

// Ordering used to surface the most severe sub-check status.
//
// Skipped ranks as the weakest result, level with Passed. "Not applicable" is
// not a problem, so a composite whose applicable sub-checks all pass must read
// as Passed. The all-skipped case is decided explicitly in evaluate(), never by
// this ranking, so an RC-less vehicle cannot report a false Passed.
int statusRank(CheckStatus s)
{
    switch (s) {
    case CheckStatus::Error:
    case CheckStatus::Failed:  return 4;
    case CheckStatus::Warning: return 3;
    case CheckStatus::Pending:
    case CheckStatus::Stale:   return 2;
    case CheckStatus::Skipped:
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
    int evaluated = 0;

    for (AbstractCheck *sub : m_subChecks) {
        sub->evaluate();
        const CheckStatus s = sub->status();

        if (s != CheckStatus::Pending)
            ++evaluated;

        if (s == CheckStatus::Passed) {
            ++passed;
            continue;
        }

        // Skipped never competes for worst: it means "does not apply", which is
        // weaker than any verified result.
        if (s != CheckStatus::Skipped && statusRank(s) > statusRank(worst))
            worst = s;

        // Retain the reason for every non-passing sub-check, including Pending
        // and Skipped, so the aggregate always names the sub-check that needs
        // attention instead of discarding the detail.
        issues << QStringLiteral("%1: %2").arg(sub->label(), sub->message());
    }

    const int total = m_subChecks.size();

    CheckStatus aggregate = CheckStatus::Pending;
    QString detail;

    if (evaluated == 0) {
        // No sub-check has produced a result yet.
        aggregate = CheckStatus::Pending;
        detail = QStringLiteral("Waiting for RC telemetry");
    } else if (worst != CheckStatus::Passed) {
        // Highest severity dominates: Failed / Error / Warning / Pending /
        // Stale, reported with the specific sub-check reasons.
        aggregate = worst;
        detail = issues.join(QStringLiteral("; "));
        if (detail.isEmpty())
            detail = QStringLiteral("RC check incomplete");
    } else if (passed > 0) {
        // Every applicable sub-check passed; skipped ones do not apply here.
        aggregate = CheckStatus::Passed;
        detail = QStringLiteral("All applicable RC checks passed (%1/%2)")
                     .arg(passed).arg(total);
    } else {
        // Nothing applies to this vehicle at all.
        aggregate = CheckStatus::Skipped;
        detail = issues.join(QStringLiteral("; "));
        if (detail.isEmpty())
            detail = QStringLiteral("No RC checks apply to this vehicle");
    }

    setCurrentValue(passed);
    setStatus(aggregate, detail);
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
