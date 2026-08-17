#include "FlightSession.h"
#include "OperatorManager.h"
#include "DatabaseManager.h"
#include "VehicleRegistry.h"
#include "WeatherProvider.h"

#include <QDebug>

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
    emit flightIdChanged();
    emit armingPermittedChanged();
    emit sessionStarted(SessionMode::Flight);
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
        DatabaseManager::instance().closeFlight(
            m_flightId, durationSec,
            m_maxAltitude, m_minBatteryV, m_maxBatteryV, m_modeChanges);
        DatabaseManager::instance().updateOperatorStats(
            OperatorManager::instance()->currentOperatorId(), true);
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
        DatabaseManager::instance().setFlightArmedAt(m_flightId, m_armedAt);
        DatabaseManager::instance().insertTelemetryEvent(
            m_flightId, QStringLiteral("ARM"), QString(),
            0.0, 0.0, 0, QString());
    }
    emit armingPermittedChanged();
}

void FlightSession::onVehicleDisarmed()
{
    _setState(SessionState::PostFlight);
    QDateTime now = QDateTime::currentDateTimeUtc();

    // Only persist to DB for audited Flight sessions
    if (m_mode == SessionMode::Flight && m_flightId > 0) {
        DatabaseManager::instance().setFlightDisarmedAt(m_flightId, now);
        DatabaseManager::instance().insertTelemetryEvent(
            m_flightId, QStringLiteral("DISARM"), QString(),
            0.0, 0.0, 0, QString());
    }
    emit postFlightChecklistRequired();
}

void FlightSession::onDisarmedWithStats(double maxAltitude, double minBatteryV, double maxBatteryV, int modeChanges)
{
    m_maxAltitude = qMax(m_maxAltitude, maxAltitude);
    if (minBatteryV >= 0.0) m_minBatteryV = qMin(m_minBatteryV, minBatteryV);
    m_maxBatteryV = qMax(m_maxBatteryV, maxBatteryV);
    m_modeChanges = qMax(m_modeChanges, modeChanges);
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
