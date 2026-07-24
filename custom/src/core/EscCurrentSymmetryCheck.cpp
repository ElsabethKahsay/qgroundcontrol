#include "EscCurrentSymmetryCheck.h"

#include "TelemetryBridge.h"

EscCurrentSymmetryCheck::EscCurrentSymmetryCheck(TelemetryBridge *telemetry,
                                                 double maxDevRatio, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.esc.current_symmetry"),
                    QStringLiteral("ESC Current Symmetry"),
                    CheckCategory::Propulsion, CheckType::Auto, false, true, parent)
    , m_maxDevRatio(maxDevRatio)
{
    m_telemetry = telemetry;
}

// Pass: all ESC currents within maxDevRatio (default 20%) of the mean.
// Warning: imbalance detected. Pending: mean current too low to evaluate (< 0.1 A).
void EscCurrentSymmetryCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant currentsVar = getTelemetryVariant(QStringLiteral("escCurrents"));
    if (!currentsVar.isValid() || currentsVar.toList().isEmpty()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No ESC telemetry"));
        return;
    }

    QVariantList currents = currentsVar.toList();
    double sum = 0.0;
    int n = qMin(currents.size(), 4);
    for (int i = 0; i < n; ++i)
        sum += currents[i].toDouble();
    double mean = sum / n;

    if (mean < 0.1) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for current data"));
        return;
    }

    setCurrentValue(mean);

    QStringList imbalanced;
    for (int i = 0; i < n; ++i) {
        double c = currents[i].toDouble();
        double dev = qAbs(c - mean) / mean;
        if (dev > m_maxDevRatio)
            imbalanced.append(QStringLiteral("ESC%1=%2A (%3% deviation)")
                                  .arg(i + 1).arg(c, 0, 'f', 2)
                                  .arg(dev * 100.0, 0, 'f', 0));
    }

    if (imbalanced.isEmpty()) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Currents balanced (mean %1 A)").arg(mean, 0, 'f', 2));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Current imbalance: %1").arg(imbalanced.join(QStringLiteral("; "))));
    }
}
