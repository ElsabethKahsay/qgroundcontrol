#include "GpsFixCheck.h"

#include "TelemetryBridge.h"

GpsFixCheck::GpsFixCheck(TelemetryBridge *telemetry, int minSatellites,
                         double maxHdop, QObject *parent)
    : AbstractCheck(QStringLiteral("nav.gps.fix"),
                    QStringLiteral("GPS Fix"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_minSatellites(minSatellites)
    , m_maxHdop(maxHdop)
{
    m_telemetry = telemetry;
}

// Fail if fix < 3D, sats < min, or HDOP > max. Warn if position is at origin (0,0).
void GpsFixCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int fixType = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsFixType")));
    int sats = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsSatellites")));
    double lat = getTelemetryDouble(QStringLiteral("gpsLatitude"));
    double lon = getTelemetryDouble(QStringLiteral("gpsLongitude"));
    double hdop = getTelemetryDouble(QStringLiteral("gpsHdop"));

    setCurrentValue(sats);

    // Param-driven thresholds with fallback to constructor defaults
    int minSats = m_minSatellites;
    double maxHdop = m_maxHdop;

    if (isParamAvailable(QStringLiteral("param_EKF2_REQ_NSATS"))) {
        double paramNsats = getTelemetryDouble(QStringLiteral("param_EKF2_REQ_NSATS"));
        minSats = static_cast<int>(paramNsats);
    } else if (isParamAvailable(QStringLiteral("param_GPS_MIN_SATS"))) {
        double paramGpsMinSats = getTelemetryDouble(QStringLiteral("param_GPS_MIN_SATS"));
        minSats = static_cast<int>(paramGpsMinSats);
    }

    if (isParamAvailable(QStringLiteral("param_GPS_HDOP_GOOD"))) {
        double paramHdop = getTelemetryDouble(QStringLiteral("param_GPS_HDOP_GOOD"));
        maxHdop = paramHdop;
    }

    if (fixType == 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No GPS fix"));
        return;
    }

    if (fixType < 3) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("2D fix only — need 3D fix"));
        return;
    }

    if (sats < minSats) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("%1 sats — need %2").arg(sats).arg(minSats));
        return;
    }

    if (hdop >= 0.0 && hdop > maxHdop) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("HDOP %1 — exceeds %2 threshold").arg(hdop, 0, 'f', 1).arg(maxHdop, 0, 'f', 1));
        return;
    }

    if (qFuzzyIsNull(lat) && qFuzzyIsNull(lon)) {
        setStatus(CheckStatus::Warning, QStringLiteral("Fix acquired but position is origin"));
        return;
    }

    QString msg = QStringLiteral("%1 sats, 3D fix").arg(sats);
    if (hdop >= 0.0)
        msg += QStringLiteral(", HDOP %1").arg(hdop, 0, 'f', 1);
    setStatus(CheckStatus::Passed, msg);
}

QString GpsFixCheck::getRationale() const
{
    return QStringLiteral("A 3D GPS fix with sufficient satellites and low HDOP is required for "
                          "accurate position estimation, navigation, and safe autonomous flight.");
}

QStringList GpsFixCheck::getFixSteps() const
{
    return {
        QStringLiteral("Move the vehicle to an open area with clear sky view"),
        QStringLiteral("Ensure the GPS antenna is unobstructed and securely connected"),
        QStringLiteral("Wait for GPS lock — this may take up to 60 seconds"),
        QStringLiteral("Check GPS_HDOP_GOOD and EKF2_REQ_NSATS parameters if fix is marginal")
    };
}

QString GpsFixCheck::getThreshold() const
{
    return QStringLiteral("3D fix, >=%1 sats, HDOP <=%2").arg(m_minSatellites).arg(m_maxHdop, 0, 'f', 1);
}

QString GpsFixCheck::getCurrentValueString() const
{
    int sats = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsSatellites")));
    double hdop = getTelemetryDouble(QStringLiteral("gpsHdop"));
    int fix = static_cast<int>(getTelemetryDouble(QStringLiteral("gpsFixType")));
    QString fixStr = fix >= 3 ? QStringLiteral("3D") : fix == 2 ? QStringLiteral("2D") : QStringLiteral("none");
    if (hdop >= 0.0)
        return QStringLiteral("%1 sats, %2 fix, HDOP %3").arg(sats).arg(fixStr).arg(hdop, 0, 'f', 1);
    return QStringLiteral("%1 sats, %2 fix").arg(sats).arg(fixStr);
}
