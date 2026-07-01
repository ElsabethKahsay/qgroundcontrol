#include "WindSpeedCheck.h"

#include "TelemetryBridge.h"

WindSpeedCheck::WindSpeedCheck(TelemetryBridge *telemetry,
                               double maxWindMps,
                               QObject *parent)
    : AbstractCheck(QStringLiteral("environment.wind"),
                    QStringLiteral("Wind Speed"),
                    CheckCategory::Environment, CheckType::Auto, false, false, parent)
    , m_maxWindMps(maxWindMps)
{
    m_telemetry = telemetry;
}

void WindSpeedCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant speedVar = getTelemetryVariant(QStringLiteral("windSpeed"));
    if (!speedVar.isValid()) {
        // Fallback: estimate wind from groundspeed vs airspeed when stationary
        double gs = getTelemetryDouble(QStringLiteral("groundSpeed"));
        double as = getTelemetryDouble(QStringLiteral("airspeed"));
        if (gs > 0.0 || as > 0.0) {
            double estWind = qAbs(gs - as);
            if (estWind <= m_maxWindMps) {
                setStatus(CheckStatus::Passed,
                          QStringLiteral("Est. %1 m/s (GS/AS delta) — OK")
                              .arg(estWind, 0, 'f', 1));
                return;
            }
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Est. %1 m/s wind from GS/AS delta — exceeds %2 m/s")
                          .arg(estWind, 0, 'f', 1).arg(m_maxWindMps, 0, 'f', 1));
            return;
        }
        setStatus(CheckStatus::Skipped, QStringLiteral("No wind data reported by vehicle"));
        return;
    }

    double speed = speedVar.toDouble();
    double dir = getTelemetryDouble(QStringLiteral("windDirection"));

    if (speed <= 0.0) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Wind data not yet available"));
        return;
    }

    if (speed <= m_maxWindMps) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 m/s — OK").arg(speed, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("%1 m/s from %2° exceeds %3 m/s")
                  .arg(speed, 0, 'f', 1)
                  .arg(dir, 0, 'f', 0)
                  .arg(m_maxWindMps, 0, 'f', 1));
}
