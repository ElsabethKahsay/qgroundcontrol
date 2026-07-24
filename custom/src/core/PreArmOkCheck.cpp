#include "PreArmOkCheck.h"

#include "TelemetryBridge.h"

PreArmOkCheck::PreArmOkCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.prearm"),
                    QStringLiteral("Pre-Arm Check"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when preArmOk is true (all vehicle pre-arm checks have cleared).
// Stays pending until the vehicle reports status, displaying the highest-severity pre-arm message.
void PreArmOkCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Read from preArmOk exposed by TelemetryBridge
    QVariant preArmVar = getTelemetryVariant(QStringLiteral("preArmOk"));
    if (!preArmVar.isValid()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Pre-arm status not yet reported"));
        return;
    }

    bool preArmOk = preArmVar.toBool();
    QString preArmMsg = m_telemetry->preArmMessage(); // signal from highest-severity pre-arm

    setCurrentValue(preArmOk);

    if (preArmOk) {
        setStatus(CheckStatus::Passed, QStringLiteral("All pre-arm checks passed"));
    } else {
        QString msg = preArmMsg.isEmpty()
                          ? QStringLiteral("Pre-arm check not yet passed")
                          : preArmMsg;
        setStatus(CheckStatus::Pending, msg);
    }
}
