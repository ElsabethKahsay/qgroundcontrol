#include "MagInterferenceCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

MagInterferenceCheck::MagInterferenceCheck(TelemetryBridge *telemetry,
                                           double maxDeltaGauss,
                                           double throttleThreshold,
                                           QObject *parent)
    : AbstractCheck(QStringLiteral("imu.mag.interference"),
                    QStringLiteral("Magnetic Interference"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxDeltaGauss(maxDeltaGauss)
    , m_throttleThreshold(throttleThreshold)
{
    m_telemetry = telemetry;
}

double MagInterferenceCheck::magMagnitude() const
{
    double mx = getTelemetryDouble(QStringLiteral("magFieldX"));
    double my = getTelemetryDouble(QStringLiteral("magFieldY"));
    double mz = getTelemetryDouble(QStringLiteral("magFieldZ"));
    if (qIsNaN(mx)) return -1.0;
    return qSqrt(mx * mx + my * my + mz * mz);
}

double MagInterferenceCheck::throttlePercent() const
{
    QVariantList channels = getTelemetryVariant(QStringLiteral("rcChannelValues")).toList();
    if (channels.size() < 3)
        return 0.0;
    // Throttle is typically channel 3 (0-indexed: 2), range 1000-2000
    double val = channels[2].toDouble();
    if (val < 900)
        return 0.0;
    return qBound(0.0, (val - 1000.0) / 1000.0, 1.0);
}

void MagInterferenceCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double mag = magMagnitude();
    if (mag < 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No magnetometer data"));
        return;
    }

    setCurrentValue(mag);

    double thr = throttlePercent();
    int minSamples = configInt(QStringLiteral("min_baseline_samples"), 5);

    // Build baseline at low throttle
    if (thr < m_throttleThreshold) {
        if (m_baselineSamples < minSamples) {
            m_baselineMag = (m_baselineMag * m_baselineSamples + mag) / (m_baselineSamples + 1);
            m_baselineSamples++;
        } else {
            // Exponentially weighted moving average for slow drift
            m_baselineMag = m_baselineMag * 0.95 + mag * 0.05;
        }
    }

    if (m_baselineSamples < minSamples) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Calibrating baseline (%1/%2)")
                      .arg(m_baselineSamples).arg(minSamples));
        return;
    }

    // Only check when throttle is active
    if (thr < m_throttleThreshold) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Idle: %1 G").arg(mag, 0, 'f', 3));
        return;
    }

    double delta = qAbs(mag - m_baselineMag);
    if (delta <= m_maxDeltaGauss) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Δ%1 G at %2% throttle — OK")
                      .arg(delta, 0, 'f', 3).arg(static_cast<int>(thr * 100.0)));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Δ%1 G at %2% throttle — possible motor interference")
                      .arg(delta, 0, 'f', 3).arg(static_cast<int>(thr * 100.0)));
    }
}
