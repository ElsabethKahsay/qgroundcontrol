#include "RcArmingSwitchCheck.h"

#include "TelemetryBridge.h"

RcArmingSwitchCheck::RcArmingSwitchCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.arming_switch"),
                    QStringLiteral("RC Arming Switch"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void RcArmingSwitchCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_ARMING_RC_ENABLE"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("ARMING_RC_ENABLE not available from vehicle"));
        return;
    }

    double armRcEnable = getTelemetryDouble(QStringLiteral("param_ARMING_RC_ENABLE"));

    int val = static_cast<int>(armRcEnable);

    if (val >= 1) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("ARMING_RC_ENABLE=%1").arg(val));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("ARMING_RC_ENABLE=0 — arming via RC disabled"));
}
