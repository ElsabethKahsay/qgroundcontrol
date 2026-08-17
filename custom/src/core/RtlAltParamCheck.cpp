#include "RtlAltParamCheck.h"

#include "TelemetryBridge.h"

RtlAltParamCheck::RtlAltParamCheck(TelemetryBridge *telemetry,
                                   double minAlt, double maxAlt, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.rtl_alt"),
                    QStringLiteral("RTL Altitude"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_minAlt(minAlt)
    , m_maxAlt(maxAlt)
{
    m_telemetry = telemetry;
}

// Converts RTL_ALT to meters (cm for ArduPilot, m for PX4 based on RTL_ALT_TYPE).
// Passes when altitude is between m_minAlt and m_maxAlt; warns if outside the range.
void RtlAltParamCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_RTL_ALT")) ||
        !isParamAvailable(QStringLiteral("param_RTL_ALT_TYPE"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("RTL params not available from vehicle"));
        return;
    }

    double rtlAlt = getTelemetryDouble(QStringLiteral("param_RTL_ALT"));
    double rtlAltType = getTelemetryDouble(QStringLiteral("param_RTL_ALT_TYPE"));

    // RTL_ALT is in cm for ArduPilot (RTL_ALT_TYPE=0), m for PX4 (RTL_ALT_TYPE=1)
    double altMeters = rtlAlt;
    if (rtlAltType <= 0.5) {
        // ArduPilot: value in cm
        altMeters /= 100.0;
    }

    setCurrentValue(altMeters);

    QString msg = QStringLiteral("%1m").arg(altMeters, 0, 'f', 0);

    if (altMeters < m_minAlt) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("%1 — below %2m minimum").arg(msg).arg(m_minAlt, 0, 'f', 0));
    } else if (altMeters > m_maxAlt) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("%1 — above %2m max recommended").arg(msg).arg(m_maxAlt, 0, 'f', 0));
    } else {
        setStatus(CheckStatus::Passed, msg + QStringLiteral(" — OK"));
    }
}
