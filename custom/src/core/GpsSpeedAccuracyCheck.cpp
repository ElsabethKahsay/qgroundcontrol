#include "GpsSpeedAccuracyCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

GpsSpeedAccuracyCheck::GpsSpeedAccuracyCheck(TelemetryBridge *telemetry,
                                             double maxSpeedErr,
                                             double maxHdop,
                                             QObject *parent)
    : AbstractCheck(QStringLiteral("nav.gps.speed_accuracy"),
                    QStringLiteral("GPS Speed Accuracy"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxSpeedErr(maxSpeedErr)
    , m_maxHdop(maxHdop)
{
    m_telemetry = telemetry;
}

void GpsSpeedAccuracyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double speedAcc = getTelemetryDouble(QStringLiteral("gpsSpeedAccuracy"));
    double hdop = getTelemetryDouble(QStringLiteral("gpsHdop"));
    double speed = getTelemetryDouble(QStringLiteral("groundSpeed"));
    int fixType = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsFixType")));

    if (fixType < 3) {
        setStatus(CheckStatus::Failed, QStringLiteral("No 3D GPS fix"));
        return;
    }

    // Primary: use vel_acc field when available
    if (speedAcc >= 0.0) {
        if (speedAcc > m_maxSpeedErr) {
            setStatus(CheckStatus::Failed,
                      QStringLiteral("GPS speed uncertainty %1 m/s — exceeds %2 m/s")
                          .arg(speedAcc, 0, 'f', 2).arg(m_maxSpeedErr, 0, 'f', 2));
            return;
        }
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Speed uncertainty %1 m/s — OK").arg(speedAcc, 0, 'f', 2));
        return;
    }

    // Fallback: HDOP-based check when vel_acc unavailable
    if (hdop < 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for GPS accuracy data"));
        return;
    }

    if (hdop > m_maxHdop) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GPS HDOP %1 exceeds %2 — position scatter may affect speed")
                      .arg(hdop, 0, 'f', 1).arg(m_maxHdop, 0, 'f', 1));
        return;
    }

    // Speed jitter detection (only when moving)
    double jmpThreshold = configDouble(QStringLiteral("speed_jmp_threshold"), 15.0);
    int maxJitter = configInt(QStringLiteral("max_jitter_samples"), 3);
    if (speed > 1.0 && m_prevSpeed > 0.0) {
        double delta = qAbs(speed - m_prevSpeed);
        if (delta > jmpThreshold) {
            m_jitterCount++;
        } else {
            m_jitterCount = qMax(0, m_jitterCount - 1);
        }
    }
    m_prevSpeed = speed;

    if (m_jitterCount >= maxJitter) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GPS speed jumping — check for interference or multipath"));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("HDOP %1, speed stable").arg(hdop, 0, 'f', 1));
}
