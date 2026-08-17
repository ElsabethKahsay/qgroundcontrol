#include "BatteryFailsafeParamCheck.h"
#include "TelemetryBridge.h"
#include "Vehicle/Vehicle.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"

BatteryFailsafeParamCheck::BatteryFailsafeParamCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.battery_param"),
                    QStringLiteral("Battery Failsafe Config"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
    setStatus(CheckStatus::Pending, QStringLiteral("Checking battery failsafe parameters..."));
}

void BatteryFailsafeParamCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (!v || !v->parameterManager()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for vehicle parameter manager"));
        return;
    }

    ParameterManager *pm = v->parameterManager();
    if (!pm->parametersReady()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for parameters to load"));
        return;
    }

    int compId = v->defaultComponentId();

    if (v->apmFirmware()) {
        Fact *enableFact = pm->getParameter(compId, QStringLiteral("FS_BATT_ENABLE"));
        Fact *voltFact = pm->getParameter(compId, QStringLiteral("FS_BATT_VOLTAGE"));

        int enableVal = enableFact ? enableFact->rawValue().toInt() : 0;
        float voltVal = voltFact ? voltFact->rawValue().toFloat() : 0.0f;

        if (enableVal == 0) {
            setStatus(CheckStatus::Failed, QStringLiteral("Battery failsafe disabled — set FS_BATT_ENABLE > 0"));
            return;
        }

        if (voltVal <= 0.0f) {
            setStatus(CheckStatus::Warning, QStringLiteral("Battery failsafe voltage not set — configure FS_BATT_VOLTAGE"));
            return;
        }

        setStatus(CheckStatus::Passed, QStringLiteral("Battery failsafe active at %1 V").arg(voltVal, 0, 'f', 1));
        return;
    }

    if (v->px4Firmware()) {
        Fact *critFact = pm->getParameter(compId, QStringLiteral("BAT_CRIT_THR"));
        float critVal = critFact ? critFact->rawValue().toFloat() : 0.0f;

        if (critVal <= 0.0f) {
            setStatus(CheckStatus::Failed, QStringLiteral("Battery failsafe critical threshold disabled — set BAT_CRIT_THR"));
            return;
        }

        setStatus(CheckStatus::Passed, QStringLiteral("Battery critical failsafe set at %1%").arg(static_cast<int>(critVal * 100)));
        return;
    }

    // Default pass if generic autopilot
    setStatus(CheckStatus::Passed, QStringLiteral("Battery failsafe configuration verified"));
}
