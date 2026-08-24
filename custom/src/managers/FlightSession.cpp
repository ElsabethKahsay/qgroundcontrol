#include "FlightSession.h"
#include "OperatorManager.h"
#include "DatabaseManager.h"
#include "VehicleRegistry.h"
#include "WeatherProvider.h"

#include <QDebug>
#include <cmath>

static FlightSession *s_instance = nullptr;

FlightSession *FlightSession::instance()
{
    return s_instance;
}

FlightSession::FlightSession(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

void FlightSession::setInstructor(int operatorId)
{
    if (operatorId <= 0) return;
    QVariantMap op = OperatorManager::instance()->getOperatorById(operatorId);
    if (op.isEmpty()) return;
    m_instructorId = operatorId;
    m_instructorName = op.value("name").toString();
    emit instructorChanged();
}

bool FlightSession::armingPermitted() const
{
    if (m_mode == SessionMode::Training)  return false;
    if (m_mode == SessionMode::None)      return false;
    if (m_mode == SessionMode::Testing)   return true;
    if (!m_formComplete)                  return false;
    if (m_state != SessionState::ReadyToArm
     && m_state != SessionState::Armed)   return false;
    return true;
}

QString FlightSession::sessionSummary() const
{
    if (m_mode == SessionMode::None) return QString();
    QString prefix = isTraining() ? QStringLiteral("Training") : (isTesting() ? QStringLiteral("Testing") : QStringLiteral("Flight"));
    QString op = OperatorManager::instance()->currentOperatorName();
    switch (m_state) {
    case SessionState::PreFlight:   return prefix + QStringLiteral(" \u2014 ") + op + QStringLiteral(" \u2014 Pre-flight");
    case SessionState::ReadyToArm:  return prefix + QStringLiteral(" \u2014 ") + op + QStringLiteral(" \u2014 Ready to Arm");
    case SessionState::Armed:       return prefix + QStringLiteral(" \u2014 ") + op + QStringLiteral(" \u2014 Armed");
    case SessionState::PostFlight:  return prefix + QStringLiteral(" \u2014 ") + op + QStringLiteral(" \u2014 Post-flight");
    default: return prefix + QStringLiteral(" \u2014 ") + op;
    }
}

void FlightSession::onVehicleConnected()
{
    if (m_state == SessionState::Idle) {
        _setState(SessionState::AwaitingModeSelect);
    }
}

void FlightSession::setMode(const QString &mode)
{
    const QString m = mode.trimmed().toUpper();
    SessionMode newMode = SessionMode::None;
    if (m == QStringLiteral("TRAINING"))      newMode = SessionMode::Training;
    else if (m == QStringLiteral("TESTING"))  newMode = SessionMode::Testing;
    else if (m == QStringLiteral("FLIGHT"))   newMode = SessionMode::Flight;
    _setMode(newMode);
}

void FlightSession::requestForm()
{
    if (m_state == SessionState::AwaitingModeSelect) {
        _setState(SessionState::FormPending);
    }
}

void FlightSession::cancelForm()
{
    if (m_state == SessionState::FormPending) {
        _setState(SessionState::AwaitingModeSelect);
    }
}

void FlightSession::startTrainingSession()
{
    _setMode(SessionMode::Training);
    m_formComplete = true;
    m_startedAt = QDateTime::currentDateTimeUtc();
    _setState(SessionState::PreFlight);
    emit armingPermittedChanged();
    emit formCompleteChanged();
    emit sessionStarted(SessionMode::Training);
}

void FlightSession::startTestingSession()
{
    _setMode(SessionMode::Testing);
    m_formComplete = true;
    m_startedAt = QDateTime::currentDateTimeUtc();
    _setState(SessionState::PreFlight);
    emit armingPermittedChanged();
    emit formCompleteChanged();
    emit sessionStarted(SessionMode::Testing);
}

void FlightSession::startFlightSession(const QString &purpose,
                                       const QString &location,
                                       const QString &notes)
{
    int operatorId = OperatorManager::instance()->currentOperatorId();
    int vehicleId = VehicleRegistry::instance()->currentVehicleId();
    if (operatorId <= 0 || vehicleId <= 0) {
        qWarning() << "FlightSession: cannot start flight — operator or vehicle not set";
        return;
    }

    QString weatherSummary;
    WeatherProvider *wp = WeatherProvider::instance();
    if (wp) {
        QString desc = wp->weatherDescription();
        if (!desc.isEmpty())
            weatherSummary = QStringLiteral("%1°C, %2, wind %3 km/h")
                .arg(wp->temperature(), 0, 'f', 1)
                .arg(desc)
                .arg(wp->windSpeed(), 0, 'f', 1);
    }

    int id = DatabaseManager::instance().openFlight(
        operatorId, vehicleId, QStringLiteral("FLIGHT"),
        purpose, location, notes, weatherSummary);
    if (id <= 0) {
        qWarning() << "FlightSession: failed to create flight DB record";
        return;
    }

    m_flightId = id;
    m_startedAt = QDateTime::currentDateTimeUtc();
    _setMode(SessionMode::Flight);
    _setState(SessionState::PreFlight);
    reloadTargetLocation();
    emit flightIdChanged();
    emit armingPermittedChanged();
    emit sessionStarted(SessionMode::Flight);
}

bool FlightSession::saveTargetLocation(double lat, double lon, const QString &source)
{
    auto fail = [this](const QString &why) {
        if (m_lastTargetError != why) {
            m_lastTargetError = why;
            emit lastTargetErrorChanged();
        }
        return false;
    };

    if (m_flightId <= 0)
        return fail(QStringLiteral("No active flight session"));
    if (!std::isfinite(lat) || !std::isfinite(lon))
        return fail(QStringLiteral("Enter a valid latitude and longitude"));
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return fail(QStringLiteral("Latitude must be \u00B190, longitude \u00B1180"));
    if (source.trimmed().isEmpty())
        return fail(QStringLiteral("Missing location source"));

    if (!DatabaseManager::instance().saveTargetLocation(m_flightId, lat, lon, source.trimmed()))
        return fail(QStringLiteral("Failed to save target location"));

    m_targetLat = lat;
    m_targetLon = lon;
    m_targetSource = source.trimmed();
    emit targetLocationChanged();

    if (!m_lastTargetError.isEmpty()) {
        m_lastTargetError.clear();
        emit lastTargetErrorChanged();
    }
    return true;
}

void FlightSession::reloadTargetLocation()
{
    QVariantMap loc = DatabaseManager::instance().getTargetLocation(m_flightId);
    if (loc.isEmpty()) {
        // No persisted location for this flight — clear the cache.
        if (!std::isnan(m_targetLat) || !std::isnan(m_targetLon)
                || !m_targetSource.isEmpty()) {
            m_targetLat = std::numeric_limits<double>::quiet_NaN();
            m_targetLon = std::numeric_limits<double>::quiet_NaN();
            m_targetSource.clear();
            emit targetLocationChanged();
        }
        return;
    }
    m_targetLat = loc.value(QStringLiteral("lat")).toDouble();
    m_targetLon = loc.value(QStringLiteral("lon")).toDouble();
    m_targetSource = loc.value(QStringLiteral("source")).toString();
    emit targetLocationChanged();
}

void FlightSession::onPreFlightComplete()
{
    if (m_state != SessionState::PreFlight) return;
    _setState(SessionState::ReadyToArm);
    emit armingPermittedChanged();
}

void FlightSession::closeSession()
{
    if (m_flightId > 0 && m_state != SessionState::Closed) {
        int durationSec = static_cast<int>(m_startedAt.secsTo(QDateTime::currentDateTimeUtc()));

        DatabaseManager &db = DatabaseManager::instance();
        QVariantMap counts = db.getCheckCountsForFlight(m_flightId);
        int anomalies = db.getAnomalyCountForFlight(m_flightId);

        db.closeFlight(
            m_flightId, durationSec,
            m_maxAltitude, m_minBatteryV, m_maxBatteryV, m_modeChanges,
            m_maxGroundSpeedMs, m_maxVerticalSpeedMs, m_distanceFlownM, m_avgBatteryV,
            counts.value(QStringLiteral("pass")).toInt(),
            counts.value(QStringLiteral("fail")).toInt(),
            counts.value(QStringLiteral("warn")).toInt(),
            anomalies);
        db.updateOperatorStats(
            OperatorManager::instance()->currentOperatorId(), true);

        // Accumulate flight time onto the connected vehicle's record so the
        // Vehicles page shows real total flight time per airframe.
        const QString fingerprint = VehicleRegistry::instance()->currentFingerprint();
        if (!fingerprint.isEmpty()) {
            db.incrementVehicleFlightTime(fingerprint, durationSec);
        }
    }

    int closedId = m_flightId;
    _setMode(SessionMode::None);
    _setState(SessionState::Idle);
    m_flightId = -1;
    m_formComplete = false;
    m_armedAt = QDateTime();
    m_startedAt = QDateTime();
    m_maxAltitude = 0.0;
    m_minBatteryV = 999.0;
    m_maxBatteryV = 0.0;
    m_modeChanges = 0;
    m_maxGroundSpeedMs = 0.0;
    m_maxVerticalSpeedMs = 0.0;
    m_distanceFlownM = 0.0;
    m_avgBatteryV = 0.0;
    m_targetLat = std::numeric_limits<double>::quiet_NaN();
    m_targetLon = std::numeric_limits<double>::quiet_NaN();
    m_targetSource.clear();
    emit targetLocationChanged();
    emit flightIdChanged();
    emit armingPermittedChanged();
    emit formCompleteChanged();
    emit sessionClosed(closedId);
}

void FlightSession::onVehicleArmed()
{
    if (m_mode == SessionMode::Training) {
        qWarning() << "FlightSession: vehicle armed during training — unexpected";
        return;
    }
    if (m_mode == SessionMode::None) return;

    m_armedAt = QDateTime::currentDateTimeUtc();
    _setState(SessionState::Armed);

    // Only persist to DB for audited Flight sessions
    if (m_mode == SessionMode::Flight && m_flightId > 0) {
        // Arm write = flights update + telemetry event → single atomic unit.
        DatabaseManager &db = DatabaseManager::instance();
        db.beginTransaction();
        bool ok = true;
        ok &= db.setFlightArmedAt(m_flightId, m_armedAt);
        ok &= db.insertTelemetryEventSnapshot(
            m_flightId, QStringLiteral("ARM"), QString(),
            0.0, 0.0, 0, QString(),
            0.0, 0.0, 0.0, 0.0, 0.0);
        if (ok) db.commitTransaction();
        else    db.rollbackTransaction();
    }
    emit armingPermittedChanged();
}

void FlightSession::onVehicleDisarmed()
{
    _setState(SessionState::PostFlight);
    QDateTime now = QDateTime::currentDateTimeUtc();

    // Only persist to DB for audited Flight sessions
    if (m_mode == SessionMode::Flight && m_flightId > 0) {
        // Disarm write = flights update + telemetry event → single atomic unit.
        DatabaseManager &db = DatabaseManager::instance();
        db.beginTransaction();
        bool ok = true;
        ok &= db.setFlightDisarmedAt(m_flightId, now);
        ok &= db.insertTelemetryEventSnapshot(
            m_flightId, QStringLiteral("DISARM"), QString(),
            0.0, 0.0, 0, QString(),
            0.0, 0.0, 0.0, 0.0, 0.0);
        if (ok) db.commitTransaction();
        else    db.rollbackTransaction();
    }
    emit postFlightChecklistRequired();
}

void FlightSession::onDisarmedWithStats(double maxAltitude, double minBatteryV, double maxBatteryV, int modeChanges,
                                        double maxGroundSpeedMs, double maxVerticalSpeedMs,
                                        double distanceFlownM, double avgBatteryV)
{
    m_maxAltitude = qMax(m_maxAltitude, maxAltitude);
    if (minBatteryV >= 0.0) m_minBatteryV = qMin(m_minBatteryV, minBatteryV);
    m_maxBatteryV = qMax(m_maxBatteryV, maxBatteryV);
    m_modeChanges = qMax(m_modeChanges, modeChanges);
    // Higher of the two possible sources wins; the logger value is
    // authoritative when present.
    if (maxGroundSpeedMs > 0.0) m_maxGroundSpeedMs = qMax(m_maxGroundSpeedMs, maxGroundSpeedMs);
    if (maxVerticalSpeedMs > 0.0) m_maxVerticalSpeedMs = qMax(m_maxVerticalSpeedMs, maxVerticalSpeedMs);
    if (distanceFlownM > 0.0) m_distanceFlownM = distanceFlownM;
    if (avgBatteryV > 0.0) m_avgBatteryV = avgBatteryV;
}

void FlightSession::onFlightModeChanged(const QString &mode)
{
    Q_UNUSED(mode)
    if (m_flightId <= 0) return;
    m_modeChanges++;
    DatabaseManager::instance().insertTelemetryEvent(
        m_flightId, QStringLiteral("MODE_CHANGE"), mode,
        0.0, 0.0, 0, mode);
}

void FlightSession::onCheckDegraded(const QString &checkId, const QString &newStatus)
{
    if (m_flightId <= 0) return;
    DatabaseManager::instance().insertTelemetryEvent(
        m_flightId, QStringLiteral("CHECK_DEGRADED"), checkId,
        0.0, 0.0, 0, newStatus);
}

void FlightSession::onBatteryWarning(double voltageV, bool isCritical)
{
    if (m_flightId <= 0) return;
    QString eventType = isCritical ? QStringLiteral("BATTERY_CRIT") : QStringLiteral("BATTERY_WARN");
    DatabaseManager::instance().insertTelemetryEvent(
        m_flightId, eventType, QString(),
        voltageV, 0.0, 0, QString());
}

void FlightSession::recordCheckResult(const QString &checkId, const QString &category,
                                       bool isPostFlight, const QString &status,
                                       const QString &message, int confirmedBy)
{
    if (m_flightId <= 0) return;
    DatabaseManager::instance().insertFlightCheckResult(
        m_flightId, checkId, category, isPostFlight,
        status, message, confirmedBy);
}

void FlightSession::_setState(SessionState s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void FlightSession::_setMode(SessionMode m)
{
    if (m_mode == m) return;
    m_mode = m;
    emit modeChanged();
}

QDateTime FlightSession::_sessionStartTime() const
{
    return m_startedAt;
}
