#include "AirspeedCheck.h"

#include "TelemetryBridge.h"
#include "Vehicle/Vehicle.h"

AirspeedCheck::AirspeedCheck(TelemetryBridge *telemetry, double minAirspeed,
                             QObject *parent)
    : AbstractCheck(QStringLiteral("sensors.airspeed"),
                    QStringLiteral("Airspeed Sensor"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_minAirspeed(minAirspeed)
{
    m_telemetry = telemetry;
}

void AirspeedCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Multirotors carry no airspeed sensor, so the measurement cannot apply.
    // Skip rather than fail — a stalled/zero reading would otherwise become a
    // false mandatory blocker. Fixed-wing and VTOL (which do fit a pitot tube)
    // keep the full check below.
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (v && v->multiRotor()) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("Not applicable — multirotor has no airspeed sensor"));
        return;
    }

    double airspeed = getTelemetryDouble(QStringLiteral("airspeed"));

    if (qIsNaN(airspeed)) {
        setStatus(CheckStatus::Pending, QStringLiteral("No airspeed data"));
        return;
    }

    setCurrentValue(airspeed);

    // Airspeed == 0 could mean sensor not present or not yet valid
    if (qFuzzyIsNull(airspeed)) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("No airspeed data — sensor not fitted or not yet valid"));
        return;
    }

    // Skip if zero (no sensor), fail if below min, pass otherwise.
    if (airspeed < m_minAirspeed) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Airspeed %1 m/s — need %2 m/s")
                      .arg(airspeed, 0, 'f', 1).arg(m_minAirspeed, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("%1 m/s").arg(airspeed, 0, 'f', 1));
}
