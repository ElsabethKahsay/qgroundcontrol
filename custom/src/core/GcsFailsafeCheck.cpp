#include "GcsFailsafeCheck.h"

#include <QVariant>
#include <QtMath>

#include "TelemetryBridge.h"
#include "Vehicle/Vehicle.h"
#include "utils/DatabaseManager.h"

namespace {
constexpr const char *kPolicyKey = "policy";
constexpr const char *kRcPrimary = "rc_primary";
constexpr const char *kTelemetryPrimary = "telemetry_primary";
}

GcsFailsafeCheck::GcsFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.gcs"),
                    QStringLiteral("GCS Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

GcsFailsafeCheck::VehicleKind GcsFailsafeCheck::vehicleKind() const
{
    if (!m_telemetry)
        return VehicleKind::Unknown;

    // TelemetryBridge exposes a vehicleType string. AbstractCheck's
    // getTelemetryVariant() also honours the unit-test override.
    const QString type = getTelemetryVariant(QStringLiteral("vehicleType")).toString().toLower();
    if (!type.isEmpty()) {
        // Check plane/VTOL first: "VTOL Tiltrotor" contains both "vtol" and "rotor".
        if (type.contains(QStringLiteral("vtol")) || type.contains(QStringLiteral("fixed"))
            || type.contains(QStringLiteral("wing")) || type.contains(QStringLiteral("plane")))
            return VehicleKind::Plane;
        if (type.contains(QStringLiteral("copter")) || type.contains(QStringLiteral("rotor"))
            || type.contains(QStringLiteral("quad")) || type.contains(QStringLiteral("hexa"))
            || type.contains(QStringLiteral("octo")) || type.contains(QStringLiteral("tri"))
            || type.contains(QStringLiteral("heli")) || type.contains(QStringLiteral("coaxial")))
            return VehicleKind::Copter;
    }

    if (Vehicle *vehicle = m_telemetry->vehicle()) {
        if (vehicle->fixedWing())
            return VehicleKind::Plane;
        if (vehicle->multiRotor())
            return VehicleKind::Copter;
    }
    return VehicleKind::Unknown;
}

int GcsFailsafeCheck::currentVehicleId() const
{
    if (m_telemetry) {
        if (Vehicle *vehicle = m_telemetry->vehicle())
            return vehicle->id();
    }
    return 0;
}

QString GcsFailsafeCheck::parameterName() const
{
    switch (vehicleKind()) {
    case VehicleKind::Plane:  return QStringLiteral("FS_GCS_ENABL");
    case VehicleKind::Copter: return QStringLiteral("FS_GCS_ENABLE");
    case VehicleKind::Unknown: break;
    }
    return {};
}

QString GcsFailsafeCheck::timeoutParameterName() const
{
    return vehicleKind() == VehicleKind::Plane ? QStringLiteral("FS_LONG_TIMEOUT")
                                               : QStringLiteral("FS_GCS_TIMEOUT");
}

QString GcsFailsafeCheck::actionParameterName() const
{
    return vehicleKind() == VehicleKind::Plane ? QStringLiteral("FS_LONG_ACTN") : QString();
}

QString GcsFailsafeCheck::settingDescription(double value) const
{
    const int v = qRound(value);
    if (vehicleKind() == VehicleKind::Plane) {
        switch (v) {
        case 0: return QStringLiteral("Disabled");
        case 1: return QStringLiteral("Heartbeat");
        case 2: return QStringLiteral("HeartbeatAndREMRSSI");
        case 3: return QStringLiteral("HeartbeatAndAUTO");
        default: break;
        }
        return QStringLiteral("unknown value");
    }
    if (vehicleKind() == VehicleKind::Copter) {
        switch (v) {
        case 0: return QStringLiteral("Disabled/NoAction");
        case 1: return QStringLiteral("RTL");
        case 2: return QStringLiteral("RTL or Continue with Mission in Auto (removed in 4.0+)");
        case 3: return QStringLiteral("SmartRTL or RTL");
        case 4: return QStringLiteral("SmartRTL or Land");
        case 5: return QStringLiteral("Land");
        case 6: return QStringLiteral("Auto DO_LAND_START or DO_RETURN_PATH_START or RTL");
        case 7: return QStringLiteral("Brake or Land");
        default: break;
        }
        return QStringLiteral("unknown value");
    }
    return QStringLiteral("unknown value");
}

QString GcsFailsafeCheck::actionDescription(double value) const
{
    switch (qRound(value)) {
    case 0: return QStringLiteral("Continue");
    case 1: return QStringLiteral("ReturnToLaunch");
    case 2: return QStringLiteral("Glide");
    case 3: return QStringLiteral("Deploy Parachute");
    case 4: return QStringLiteral("Auto");
    case 5: return QStringLiteral("AUTOLAND");
    default: break;
    }
    return QStringLiteral("unknown value");
}

QString GcsFailsafeCheck::policy() const
{
    const QString stored =
        DatabaseManager::instance().getCheckConfig(m_id, QString::fromLatin1(kPolicyKey),
                                                   currentVehicleId());
    if (stored == QLatin1String(kRcPrimary) || stored == QLatin1String(kTelemetryPrimary))
        return stored;
    return {};
}

bool GcsFailsafeCheck::setPolicy(const QString &policy)
{
    if (policy != QLatin1String(kRcPrimary) && policy != QLatin1String(kTelemetryPrimary))
        return false;

    if (!DatabaseManager::instance().setCheckConfig(m_id, QString::fromLatin1(kPolicyKey),
                                                    policy, currentVehicleId()))
        return false;

    emit policyChanged();
    evaluate();
    return true;
}

void GcsFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    const QString param = parameterName();
    if (param.isEmpty()) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("GCS-failsafe check supports ArduPilot Plane and Copter only"));
        return;
    }

    const QString chosenPolicy = policy();
    if (chosenPolicy.isEmpty()) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GCS-failsafe policy not chosen for this airframe"));
        return;
    }

    const QString prop = QStringLiteral("param_") + param;
    if (!isParamAvailable(prop)) {
        setStatus(CheckStatus::Skipped, QStringLiteral("%1 not available from vehicle").arg(param));
        return;
    }

    const double setting = getTelemetryDouble(prop);
    setCurrentValue(setting);
    const QString description = settingDescription(setting);

    if (chosenPolicy == QLatin1String(kRcPrimary)) {
        if (qRound(setting) == 0) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("%1 = 0 (Disabled) — RC-primary policy: GCS loss does not "
                                     "trigger a failsafe").arg(param));
            return;
        }
        setStatus(CheckStatus::Failed,
                  QStringLiteral("%1 = %2 (%3), but this airframe is RC-primary (expected OFF): "
                                 "a telemetry dropout would take control from the pilot")
                      .arg(param).arg(qRound(setting)).arg(description));
        return;
    }

    if (qRound(setting) != 0) {
        QString message = QStringLiteral("%1 = %2 (%3) — telemetry-primary policy")
                              .arg(param).arg(qRound(setting)).arg(description);
        const QString timeoutParam = timeoutParameterName();
        const QString timeoutProp = QStringLiteral("param_") + timeoutParam;
        if (isParamAvailable(timeoutProp)) {
            message += QStringLiteral("; %1 = %2 s")
                           .arg(timeoutParam)
                           .arg(QString::number(getTelemetryDouble(timeoutProp), 'f', 1));
        }
        const QString actionParam = actionParameterName();
        const QString actionProp = QStringLiteral("param_") + actionParam;
        if (!actionParam.isEmpty() && isParamAvailable(actionProp)) {
            const double action = getTelemetryDouble(actionProp);
            message += QStringLiteral("; %1 = %2 (%3)")
                           .arg(actionParam).arg(qRound(action)).arg(actionDescription(action));
        }
        setStatus(CheckStatus::Passed, message);
        return;
    }

    setStatus(CheckStatus::Failed,
              QStringLiteral("%1 = 0 (Disabled), but this airframe is telemetry-primary "
                             "(expected ON): a GCS link loss would leave no failsafe")
                  .arg(param));
}

QString GcsFailsafeCheck::getRationale() const
{
    return QStringLiteral("With RC as the primary control, a telemetry dropout must not take "
                          "control from the pilot; with the GCS link as the primary command path, "
                          "losing it must trigger the aircraft's configured failsafe. The policy "
                          "is chosen per airframe and stored in the GCS; the GCS never writes the "
                          "vehicle parameter.");
}

QStringList GcsFailsafeCheck::getFixSteps() const
{
    const QString param = parameterName();
    const QString chosenPolicy = policy();

    if (param.isEmpty()) {
        return {QStringLiteral("Connect an ArduPilot Plane or Copter; this check does not apply "
                               "to other vehicle types")};
    }
    if (chosenPolicy.isEmpty()) {
        return {QStringLiteral("Choose RC-primary or Telemetry-primary on this check card"),
                QStringLiteral("Agree the policy for this airframe before changing %1").arg(param)};
    }

    if (chosenPolicy == QLatin1String(kRcPrimary)) {
        return {QStringLiteral("Set %1 = 0 on the aircraft in QGC Parameters").arg(param),
                QStringLiteral("Or choose Telemetry-primary on this card if the GCS link is the "
                               "primary control path")};
    }

    QStringList steps;
    if (vehicleKind() == VehicleKind::Plane) {
        steps << QStringLiteral("Set %1 = 1 (Heartbeat) on the aircraft in QGC Parameters; use "
                                "2 or 3 only if the radio supports REMRSSI or AUTO-only")
                     .arg(param);
        steps << QStringLiteral("Set FS_LONG_TIMEOUT (default 5 s) and FS_LONG_ACTN "
                                "(e.g. 1 = ReturnToLaunch) to the intended values");
    } else {
        steps << QStringLiteral("Set %1 to 1 (RTL) or another enabled action on the aircraft in "
                                "QGC Parameters").arg(param);
        steps << QStringLiteral("Set FS_GCS_TIMEOUT (default 5 s) to the intended action delay");
    }
    steps << QStringLiteral("Or choose RC-primary on this card if a telemetry dropout must not "
                            "trigger a failsafe");
    return steps;
}

QString GcsFailsafeCheck::getThreshold() const
{
    const QString param = parameterName();
    if (param.isEmpty())
        return {};
    return QStringLiteral("RC-primary: %1 = 0 (Off). Telemetry-primary: %1 >= 1 (enabled).")
        .arg(param);
}
