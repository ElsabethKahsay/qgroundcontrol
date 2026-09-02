#include "PreflightManager.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>

#include "FactSystem/Fact.h"
#include "FactSystem/ParameterManager.h"
#include "Vehicle/Vehicle.h"

#include "AbstractCheck.h"
#include "AccelConsistencyCheck.h"
#include "AirspeedCheck.h"
#include "AlertManager.h"
#include "AttitudeCheck.h"
#include "BatteryFailsafeCheck.h"
#include "BatteryTemperatureCheck.h"
#include "BatteryVoltageCheck.h"
#include "CellConfigCheck.h"
#include "DatabaseManager.h"
#include "EkfFailsafeCheck.h"
#include "GcsFailsafeCheck.h"
#include "GimbalLinkCheck.h"
#include "GpsFixCheck.h"
#include "HeartbeatCheck.h"
#include "HomePositionCheck.h"
#include "ImuTemperatureCheck.h"
#include "ManualConfirmCheck.h"
#include "MavlinkProtocolCheck.h"
#include "MotorCountCheck.h"
#include "MotorSpinCheck.h"
#include "MotorTemperatureCheck.h"
#include "ParameterWatchlist.h"
#include "RadioFailsafeCheck.h"
#include "RcArmingSwitchCheck.h"
#include "RcCalibrationCheck.h"
#include "RcFailsafeCheck.h"
#include "RcModeSwitchCheck.h"
#include "RcThrottleMinCheck.h"
#include "RtlAltParamCheck.h"
#include "TelemetryBridge.h"
#include "TelemetryDropRateCheck.h"
#include "VideoFeedCheck.h"
#include "ZoneComplianceCheck.h"

#include <algorithm>

Q_LOGGING_CATEGORY(preflightLog, "qgc.custom.preflight")

// --- QML list property accessors ---
// These static functions allow QML to iterate over checks via QQmlListProperty<AbstractCheck>.

QQmlListProperty<AbstractCheck> PreflightManager::qmlChecks() {
  return QQmlListProperty<AbstractCheck>(this, &m_checks,
                                         nullptr, // append (read-only)
                                         &PreflightManager::qmlChecksCount,
                                         &PreflightManager::qmlChecksAt,
                                         nullptr // clear (read-only)
  );
}

qsizetype PreflightManager::qmlChecksCount(QQmlListProperty<AbstractCheck> *list) {
  return static_cast<QVector<AbstractCheck *> *>(list->data)->size();
}

AbstractCheck *
PreflightManager::qmlChecksAt(QQmlListProperty<AbstractCheck> *list,
                              qsizetype index) {
  return static_cast<QVector<AbstractCheck *> *>(list->data)->at(index);
}

PreflightManager::PreflightManager(uint8_t vehicleSysId, QObject *parent)
    : QObject(parent), m_sysId(vehicleSysId), m_timer(new QTimer(this)),
      m_stateMachine(this), m_paramManager(this) {
  m_timer->setSingleShot(false);
  // Timer drives the periodic evaluation cycle via tick()
  connect(m_timer, &QTimer::timeout, this, &PreflightManager::tick);
  connect(&m_stateMachine, &PreflightStateMachine::stateChanged, this,
          &PreflightManager::stateChanged);

  // Create all built-in checks immediately at construction
  createPhase1Checks();
}

PreflightManager::~PreflightManager() {
  qDeleteAll(m_checks);
  m_checks.clear();
}

void PreflightManager::setTelemetryBridge(TelemetryBridge *bridge) {
  if (m_telemetry == bridge)
    return;
  m_telemetry = bridge;

  // Push the telemetry bridge to all registered checks so they can read values
  for (auto *check : m_checks) {
    check->m_telemetry = bridge;
  }

  if (m_telemetry) {
    // Set up autopilot-specific parameter mapping callbacks. These fire when
    // the autopilot type is detected (PX4 vs ArduPilot) and apply the correct
    // parameter naming convention.
    m_autopilotDetector.setPx4ParamMapLoader([this]() {
      qDebug() << "PX4 autopilot detected — applying PX4 parameter defaults";
      if (m_telemetry) {
        m_telemetry->setParameterValue(QStringLiteral("PX4_GPS_MIN_SATS"),
                                       8.0f);
        m_telemetry->setParameterValue(QStringLiteral("PX4_CELL_COUNT_DEFAULT"),
                                       6.0f);
      }
    });
    m_autopilotDetector.setArduParamMapLoader([this]() {
      qDebug() << "ArduPilot autopilot detected — applying ArduPilot parameter "
                  "defaults";
      if (m_telemetry) {
        m_telemetry->setParameterValue(QStringLiteral("AP_GPS_MIN_SATS"), 6.0f);
        m_telemetry->setParameterValue(QStringLiteral("AP_CELL_COUNT_DEFAULT"),
                                       6.0f);
      }
    });

    // Detect autopilot when vehicle is connected
    auto detectAutopilot = [this]() {
      Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
      if (!v)
        return;
      if (v->px4Firmware())
        m_autopilotDetector.consumeHeartbeat(12); // MAV_AUTOPILOT_PX4
      else if (v->apmFirmware())
        m_autopilotDetector.consumeHeartbeat(3); // MAV_AUTOPILOT_ARDUPILOTMEGA
      else
        m_autopilotDetector.consumeHeartbeat(0); // MAV_AUTOPILOT_GENERIC
    };
    detectAutopilot();

    // When the vehicle connects, transition to ParamLoading and detect autopilot type
    connect(m_telemetry, &TelemetryBridge::isConnectedChanged, this, [this]() {
      if (m_telemetry->isConnected()) {
        m_stateMachine.transitionTo(PreflightStateMachine::ParamLoading);
        Vehicle *v = m_telemetry->vehicle();
        if (v) {
          if (v->px4Firmware())
            m_autopilotDetector.consumeHeartbeat(12);
          else if (v->apmFirmware())
            m_autopilotDetector.consumeHeartbeat(3);
        }
        evaluateAll();
      } else {
        m_stateMachine.transitionTo(PreflightStateMachine::Disconnected);
        resetAll();
      }
    });
    // Once QGC's ParameterManager reports parameters are ready, pull in the
    // watchlist values and move to ChecklistInProgress state
    connect(m_telemetry, &TelemetryBridge::parametersReadyChanged, this,
            [this](bool ready) {
              if (ready) {
                _initialParamRefresh();
                m_stateMachine.transitionTo(
                    PreflightStateMachine::ChecklistInProgress);
                evaluateAll();
              }
            });
    // Forward individual parameter updates to the UavParameterManager
    connect(m_telemetry, &TelemetryBridge::parameterUpdated, this,
            [this](const QString &name, float value) {
              m_paramManager.notifyParamReceived(name);
              m_paramManager.storeParam(name, value);
            });
  }
}

void PreflightManager::setAlertManager(AlertManager *alertManager) {
  m_alertManager = alertManager;
}

void PreflightManager::setDatabaseManager(DatabaseManager *db) {
  m_db = db;
  loadOverrides();
}

AbstractCheck *PreflightManager::checkById(const QString &checkId) const {
  for (auto *check : m_checks) {
    if (check->id() == checkId)
      return check;
  }
  return nullptr;
}

int PreflightManager::checksInCategory(int categoryId) const {
  int count = 0;
  for (auto *c : m_checks) {
    if (static_cast<int>(c->category()) == categoryId)
      count++;
  }
  return count;
}

int PreflightManager::passedInCategory(int categoryId) const {
  int count = 0;
  for (auto *c : m_checks) {
    if (static_cast<int>(c->category()) == categoryId &&
        c->status() == CheckStatus::Passed)
      count++;
  }
  return count;
}

bool PreflightManager::hasCategory(int categoryId) const {
  for (auto *c : m_checks) {
    if (static_cast<int>(c->category()) == categoryId)
      return true;
  }
  return false;
}

QObjectList
PreflightManager::checksForCategory(const QVariantList &categories) const {
  QObjectList result;
  for (auto *c : m_checks) {
    int cat = static_cast<int>(c->category());
    for (const auto &v : categories) {
      if (cat == v.toInt()) {
        result.append(c);
        break;
      }
    }
  }
  qDebug().noquote() << "checksForCategory(" << categories << ") ->"
                     << result.size() << "checks";
  return result;
}

// Return checks sorted by priority: mandatory failures first, then warnings,
// then non-mandatory issues. Used by QML to display blocking checks in order.
QVariantList PreflightManager::blockingChecks() const {
  QVector<AbstractCheck *> sorted = m_checks;
  sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
                              [](AbstractCheck *c) {
                                return c->status() == CheckStatus::Passed;
                              }),
               sorted.end());
  std::sort(
      sorted.begin(), sorted.end(), [](AbstractCheck *a, AbstractCheck *b) {
        auto prio = [](AbstractCheck *c) -> int {
          auto s = c->status();
          bool fail = (s == CheckStatus::Failed || s == CheckStatus::Error);
          bool warn = (s == CheckStatus::Warning);
          if (c->mandatory() && fail)
            return 0;
          if (c->mandatory() && warn)
            return 1;
          if (c->mandatory() && !fail && !warn)
            return 2;
          if (!c->mandatory() && fail)
            return 3;
          if (!c->mandatory() && warn)
            return 4;
          return 5;
        };
        return prio(a) < prio(b);
      });
  QVariantList result;
  for (auto *c : sorted)
    result.append(QVariant::fromValue(c));
  return result;
}

int PreflightManager::blockingCount() const {
  int count = 0;
  for (auto *c : m_checks) {
    if (c->status() != CheckStatus::Passed)
      count++;
  }
  return count;
}

int PreflightManager::passedChecks() const {
  int count = 0;
  for (auto *c : m_checks) {
    if (c->status() == CheckStatus::Passed)
      count++;
  }
  return count;
}

int PreflightManager::failedChecks() const {
  int count = 0;
  for (auto *c : m_checks) {
    auto s = c->status();
    if (s == CheckStatus::Failed || s == CheckStatus::Stale)
      count++;
  }
  return count;
}

int PreflightManager::pendingChecks() const {
  int count = 0;
  for (auto *c : m_checks) {
    if (c->status() == CheckStatus::Pending ||
        c->status() == CheckStatus::Warning)
      count++;
  }
  return count;
}

int PreflightManager::blockingFailedCount() const {
  return std::count_if(m_checks.begin(), m_checks.end(), [](AbstractCheck *c) {
    auto s = c->status();
    return c->mandatory() &&
           (s == CheckStatus::Failed || s == CheckStatus::Error || s == CheckStatus::Stale);
  });
}

QString PreflightManager::firstBlockingFailure() const {
  for (auto *c : m_checks) {
    auto s = c->status();
    if (c->mandatory() &&
        (s == CheckStatus::Failed || s == CheckStatus::Error || s == CheckStatus::Stale))
      return c->label();
  }
  return {};
}

int PreflightManager::completionPercent() const {
  if (m_checks.isEmpty())
    return 0;
  return (passedChecks() * 100) / m_checks.size();
}

int PreflightManager::staleCount() const {
  int count = 0;
  for (auto *c : m_checks) {
    if (c->status() == CheckStatus::Stale)
      count++;
  }
  return count;
}

bool PreflightManager::allMandatoryPassed() const {
  for (auto *c : m_checks) {
    auto s = c->status();
    if (c->mandatory() && s != CheckStatus::Passed)
      return false;
  }
  return true;
}

// Progress as fraction of checks that have been evaluated (non-Pending).
double PreflightManager::progress() const {
  if (m_checks.isEmpty())
    return 0.0;
  int evaluated = 0;
  for (auto *c : m_checks) {
    if (c->status() != CheckStatus::Pending)
      evaluated++;
  }
  return (double)evaluated / (double)m_checks.size();
}

// Find the most urgent arming blocker message.
// Priority order: Stale > Error > Failed (first mandatory match wins).
QString PreflightManager::armingBlocker() const {
  // Prioritize: stale → error → failed
  for (auto *c : m_checks) {
    if (c->mandatory() && c->status() == CheckStatus::Stale)
      return c->label() + QStringLiteral(": ") + c->message();
  }
  for (auto *c : m_checks) {
    if (c->mandatory() && c->status() == CheckStatus::Error)
      return c->label() + QStringLiteral(": ") + c->message();
  }
  for (auto *c : m_checks) {
    if (c->mandatory() && c->status() == CheckStatus::Failed)
      return c->label() + QStringLiteral(": ") + c->message();
  }
  return {};
}

void PreflightManager::startEvaluation(int intervalMs) {
  m_evalIntervalMs = intervalMs;
  if (!m_active) {
    m_active = true;
    emit activeChanged(true);
  }
  m_timer->start(m_evalIntervalMs);
  evaluateAll();
}

void PreflightManager::stopEvaluation() { m_timer->stop(); }

// Evaluate all Auto checks in sequence. Manual/Action checks are skipped
// (they require explicit user confirmation). Catches exceptions to prevent
// a single misbehaving check from breaking the entire evaluation cycle.
void PreflightManager::evaluateAll() {
  for (auto *check : m_checks) {
    if (!check->isAuto())
      continue;
    try {
      check->evaluate();
    } catch (const std::exception &e) {
      qWarning() << "Check evaluation failed for" << check->id() << ":"
                 << e.what();
      check->setStatus(CheckStatus::Error,
                       QStringLiteral("Evaluation error: ") +
                           QString::fromUtf8(e.what()));
    } catch (...) {
      qWarning() << "Unknown error evaluating check" << check->id();
      check->setStatus(CheckStatus::Error,
                       QStringLiteral("Unknown evaluation error"));
    }
  }

  emit modelChanged();
  emit progressChanged();

  attemptStateTransition();

  QString blocker = armingBlocker();
  if (blocker != m_lastBlocker) {
    m_lastBlocker = blocker;
    emit armingBlockerChanged(blocker);
  }
  if (!blocker.isEmpty()) {
    emit armingBlocked(m_sysId, blocker);
  }
}

// Advance or retreat the state machine based on current check results.
//
// Decision logic:
//   - All mandatory passed (including manual) → ArmingAllowed
//   - All auto-mandatory passed (manual still pending) → PreflightPass → ManualConfirmPhase
//   - Otherwise → ChecklistInProgress (or retreat from ArmingAllowed if checks degrade)
void PreflightManager::attemptStateTransition() {
  bool allMandatoryPass = allMandatoryPassed();
  bool allAutoMandatoryPass = true;
  for (auto *c : m_checks) {
    if (c->mandatory() && c->isAuto() && c->status() != CheckStatus::Passed) {
      allAutoMandatoryPass = false;
      break;
    }
  }
//??
  if (allMandatoryPass) {
    emit allChecksPassed(m_sysId);
    if (m_stateMachine.state() < PreflightStateMachine::ArmingAllowed)
      m_stateMachine.transitionTo(PreflightStateMachine::ArmingAllowed);
  } else if (allAutoMandatoryPass) {
    if (m_stateMachine.state() < PreflightStateMachine::PreflightPass)
      m_stateMachine.transitionTo(PreflightStateMachine::PreflightPass);
    if (m_stateMachine.state() == PreflightStateMachine::PreflightPass)
      m_stateMachine.transitionTo(PreflightStateMachine::ManualConfirmPhase);
  } else {
    if (m_stateMachine.state() <= PreflightStateMachine::ParamLoading)
      m_stateMachine.transitionTo(PreflightStateMachine::ChecklistInProgress);
    else if (m_stateMachine.state() >= PreflightStateMachine::ArmingAllowed)
      m_stateMachine.transitionTo(PreflightStateMachine::ChecklistInProgress);
  }
}

void PreflightManager::resetAll() {
  for (auto *check : m_checks) {
    check->reset();
  }
  emit modelChanged();
  emit progressChanged();
}

void PreflightManager::activatePostFlightChecks() {
  for (auto *check : m_checks) {
    if (check->isPostFlight()) {
      check->reset();
    } else {
      check->overrideStatus(QStringLiteral("Skipped"), QStringLiteral("Post-flight checklist"));
    }
  }
  emit modelChanged();
  emit progressChanged();
}

void PreflightManager::setActive(bool active) {
  if (m_active == active)
    return;
  m_active = active;
  emit activeChanged(active);
  if (active)
    startEvaluation(m_evalIntervalMs);
  else
    stopEvaluation();
}

// Main timer tick: drives the periodic evaluation cycle.
//
// Performs four tasks each tick:
//   1. Manages the parameter-loading state machine (SYS-001)
//   2. Propagates autopilot type detection to the parameter manager (SYS-002)
//   3. Re-evaluates stale Auto checks
//   4. Checks staleness of mandatory checks and marks them Stale if data is too old
//
// Emits progressChanged (not modelChanged) to avoid QML delegate destruction.
void PreflightManager::tick() {
  Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;

  // ── SYS-001: Parameter loading state machine ──
    if (vehicle && vehicle->parameterManager()) {
        ParameterManager *pm = vehicle->parameterManager();
        int compId = vehicle->defaultComponentId();
        const QSet<QString> watchlist = ParameterWatchlist::names();

        if (pm->parametersReady()) {
          // Transition state machine on param readiness
      if (m_stateMachine.state() <= PreflightStateMachine::Connecting) {
        m_stateMachine.transitionTo(PreflightStateMachine::ParamLoading);
      }
      if (m_paramManager.status() == UavParameterManager::Loading) {
        // Check which watchlist params have been received
        for (const QString &name : watchlist) {
          Fact *fact = pm->getParameter(compId, name);
          if (fact) {
            m_paramManager.notifyParamReceived(name);
          }
        }
      }
      if (m_paramManager.isReady() &&
          m_stateMachine.state() == PreflightStateMachine::ParamLoading) {
        m_stateMachine.transitionTo(PreflightStateMachine::ChecklistInProgress);
      }
    } else {
      // Parameters not yet ready — still in Connecting/ParamLoading
      if (m_telemetry->heartbeatReceived() &&
          m_stateMachine.state() == PreflightStateMachine::Disconnected) {
        m_stateMachine.transitionTo(PreflightStateMachine::Connecting);
      }
    }
  } else if (m_telemetry && m_telemetry->heartbeatReceived() &&
             m_stateMachine.state() == PreflightStateMachine::Disconnected) {
    m_stateMachine.transitionTo(PreflightStateMachine::Connecting);
  }

  // ── SYS-002: Autopilot type propagation to param manager ──
  if (m_autopilotDetector.isDetected() &&
      m_paramManager.autopilotType() == UavParameterManager::Generic) {
    if (m_autopilotDetector.autopilot() == AutopilotInfoDetector::PX4)
      m_paramManager.setAutopilotType(UavParameterManager::PX4);
    else if (m_autopilotDetector.autopilot() ==
             AutopilotInfoDetector::ArduPilot)
      m_paramManager.setAutopilotType(UavParameterManager::ArduPilot);
  }

  // ── Evaluate checks that need reevaluation ──
  for (auto *check : m_checks) {
    if (check->requiresReevaluation()) {
      try {
        check->evaluate();
      } catch (const std::exception &e) {
        qWarning() << "Tick evaluation failed for" << check->id() << ":"
                   << e.what();
        check->setStatus(CheckStatus::Error, QStringLiteral("Tick error: ") +
                                                 QString::fromUtf8(e.what()));
      } catch (...) {
        qWarning() << "Unknown error in tick for" << check->id();
        check->setStatus(CheckStatus::Error,
                         QStringLiteral("Unknown tick error"));
      }
    }
  }

  // ── Debug: log non-passed check summary ──
  if (preflightLog().isDebugEnabled()) {
    int pendingCount = 0, failedCount = 0, warnCount = 0, skipCount = 0, passCount = 0;
    for (auto *check : m_checks) {
      switch (check->status()) {
        case CheckStatus::Pending:  pendingCount++;  break;
        case CheckStatus::Failed:   failedCount++;   break;
        case CheckStatus::Warning:  warnCount++;     break;
        case CheckStatus::Skipped:  skipCount++;     break;
        default:                    passCount++;     break;
      }
    }
    static int lastSummary = 0;
    int now = QDateTime::currentMSecsSinceEpoch() / 5000;
    if (now != lastSummary) {
      lastSummary = now;
      qCDebug(preflightLog).noquote() << QStringLiteral("[Preflight] %1/91 passed | Pending:%2 Failed:%3 Warning:%4 Skipped:%5")
        .arg(passCount).arg(pendingCount).arg(failedCount).arg(warnCount).arg(skipCount);

      for (auto *check : m_checks) {
        if (check->status() != CheckStatus::Passed) {
          QString statusStr;
          switch (check->status()) {
            case CheckStatus::Pending: statusStr = "PENDING"; break;
            case CheckStatus::Failed:  statusStr = "FAILED";  break;
            case CheckStatus::Warning: statusStr = "WARNING"; break;
            case CheckStatus::Skipped: statusStr = "SKIPPED"; break;
            default:                   statusStr = "OTHER";   break;
          }
          QString val = check->currentValue().isValid()
            ? check->currentValue().toString() : QStringLiteral("-");
          qCDebug(preflightLog).noquote() << QStringLiteral("  [%1] %2 — %3").arg(statusStr, -8).arg(check->id()).arg(val);
        }
      }
    }
  }

  // ── State machine: check-result transitions (single authoritative path) ──
  attemptStateTransition();

  // ── Staleness detection: mark mandatory checks as Stale if their last
  // evaluation time exceeds the threshold (default 2x expected message rate).
  // Stale checks count as failed for arming decisions.
  for (auto *check : m_checks) {
    if (!check->mandatory())
      continue;
    auto s = check->status();
    if (s == CheckStatus::Passed || s == CheckStatus::Warning) {
      QDateTime lastEval = check->lastEvaluationTime();
      if (lastEval.isValid() &&
          lastEval.msecsTo(QDateTime::currentDateTime()) > m_stalenessThresholdMs) {
        check->setStatus(CheckStatus::Stale,
                         QStringLiteral("Data stale — last update %1s ago")
                             .arg(lastEval.secsTo(QDateTime::currentDateTime())));
      }
    } else if (s == CheckStatus::Stale) {
      QDateTime lastEval = check->lastEvaluationTime();
      if (lastEval.isValid() &&
          lastEval.msecsTo(QDateTime::currentDateTime()) <= m_stalenessThresholdMs) {
        // Data returned: recovery. The next evaluate() cycle will set the
        // correct real status.
        check->setStatus(CheckStatus::Pending,
                         QStringLiteral("Recovering from stale data"));
      }
    }
  }

  // Only emit progressChanged — this triggers dataChanged() in the model,
  // which updates bindings without destroying delegates.
  // Do NOT emit modelChanged() here; that triggers rebuild() which does
  // beginResetModel/endResetModel and destroys all QML delegates every tick.
  emit progressChanged();

  QString blocker = armingBlocker();
  if (blocker != m_lastBlocker) {
    m_lastBlocker = blocker;
    emit armingBlockerChanged(blocker);
  }
  if (!blocker.isEmpty()) {
    emit armingBlocked(m_sysId, blocker);
  }
}

// Pull initial parameter values from QGC's ParameterManager for all watchlist entries.
// Called once when parameters become ready, to seed UavParameterManager with values
// before the first check evaluation.
void PreflightManager::_initialParamRefresh()
{
    Vehicle *v = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (!v || !v->parameterManager())
        return;
    ParameterManager *pm = v->parameterManager();
    if (!pm->parametersReady())
        return;
    const int compId = v->defaultComponentId();
    const QSet<QString> watchlist = ParameterWatchlist::names();

    for (const QString &name : watchlist) {
        Fact *fact = pm->getParameter(compId, name);
        if (fact) {
            float val = fact->rawValue().toFloat();
            m_paramManager.notifyParamReceived(name);
            m_paramManager.storeParam(name, val);
        }
    }
    qCDebug(preflightLog) << "Initial parameter refresh completed for" << watchlist.size() << "params";
}

void PreflightManager::addCheck(AbstractCheck *check) {
  if (!check) return;
  m_checks.append(check);
  connectCheckSignals(check);
  emit modelChanged();
}

// Apply vehicle-kind-aware blocking rules. Called when the vehicle type
// resolves so arming never blocks on a check that cannot apply to the
// connected airframe.
//
// Multirotor:  motor count + motor spin BLOCK; airspeed / RTL alt advisory.
// Fixed wing:  airspeed + RTL alt BLOCK; motor count / motor spin advisory.
// VTOL conv.:  same as fixed wing.
// Unknown:     leave defaults untouched.
void PreflightManager::applyVehicleKind(const QString &kindString) {
  updateCriticalChecks(kindString);
  evaluateAll();
  emit modelChanged();
  emit progressChanged();
}

void PreflightManager::updateCriticalChecks(const QString &kindString) {
  const QString kind = kindString.toUpper();

  const bool multirotor = (kind == QStringLiteral("MULTIROTOR"));
  const bool flightVehicle = (kind == QStringLiteral("FIXED_WING")
                           || kind == QStringLiteral("VTOL_CONVENTIONAL"));

  // Unknown / unresolved kind: leave any previously-applied decisions untouched
  // so they are not silently reset before the airframe is identified.
  if (!multirotor && !flightVehicle)
    return;

  for (AbstractCheck *check : m_checks) {
    if (!check) continue;
    const QString id = check->id();

    if (id == QStringLiteral("airframe.motor_count")
        || id == QStringLiteral("propulsion.motors.spin")) {
      // Wrong motor count / non-responding motor = asymmetric thrust for
      // multirotors (blocks arming). For fixed wing these are advisory.
      check->setMandatory(multirotor);
    } else if (id == QStringLiteral("sensors.airspeed")
               || id == QStringLiteral("safety.rtl_alt")) {
      // Airspeed + RTL altitude are safety-critical for fixed wing; on a
      // multirotor airspeed must NEVER block arming.
      check->setMandatory(flightVehicle);
    } else if (check->isAuto()) {
      // With an identified airframe, the remaining auto safety checks
      // (battery, failsafe params, link, GPS, EKF, ...) block arming again.
      check->setMandatory(true);
    }
  }
}

// Create and register all built-in preflight checks.
// Checks are organized by tier/priority and category, then sorted by category
// for deterministic display order. Duplicate IDs are removed as a safety measure.
void PreflightManager::createPhase1Checks() {
  // ── New SRS coverage additions ──
  m_checks.append(new MavlinkProtocolCheck(m_telemetry, this));

  // Airspace: planned route vs no-fly zones.  The zone model and mission
  // controller are wired in later by PreflightPlugin (they don't exist yet
  // at this point); until then the check reports "No mission loaded".
  m_checks.append(new ZoneComplianceCheck(nullptr, this));

  // Auto checks (evaluated programmatically)
  m_checks.append(new BatteryVoltageCheck(m_telemetry, 0.0, 5.0, this));
  m_checks.append(new AttitudeCheck(m_telemetry, 30.0, this));
  m_checks.append(new HeartbeatCheck(m_telemetry, 10, this));
  m_checks.append(new RtlAltParamCheck(m_telemetry, 10.0, 122.0, this));
  m_checks.append(new HomePositionCheck(m_telemetry, 0.005, this));
  m_checks.append(new AirspeedCheck(m_telemetry, 20.0, this));

  // Tier 1 — Navigation
  m_checks.append(new AccelConsistencyCheck(m_telemetry, 4.0, this));
  m_checks.append(new GpsFixCheck(m_telemetry, 8, 2.0, this));

  // Tier 1 — Power
  m_checks.append(new BatteryTemperatureCheck(m_telemetry, 45.0, 0.0, this));
  m_checks.append(new CellConfigCheck(m_telemetry, 3.0, 1, this));

  // Tier 1 — Communication
  m_checks.append(new TelemetryDropRateCheck(m_telemetry, 10, 5, this));
  m_checks.append(new RcThrottleMinCheck(m_telemetry, this));

  // Tier 2 — rc communication
  m_checks.append(new RcModeSwitchCheck(m_telemetry, this));
  m_checks.append(new RcArmingSwitchCheck(m_telemetry, this));
  m_checks.append(new RcCalibrationCheck(m_telemetry, this));

  // Tier 3 — Warning / non-blocking auto-checks/ side bar info
  m_checks.append(new MotorCountCheck(m_telemetry, this));

  // Manual check (operator must confirm)
  m_checks.append(new MotorSpinCheck(m_telemetry, this));

  // Tier 4 — Enhancement / Niche checks
  m_checks.append(new GimbalLinkCheck(m_telemetry, this));
  m_checks.append(new ImuTemperatureCheck(m_telemetry, 85.0, -20.0, this));
  m_checks.append(new MotorTemperatureCheck(m_telemetry, 80.0, this));

  // ── Phase 3: Manual confirm checks ──

  // Airframe (2 remaining; antenna placement folded into visual inspection)
  m_checks.append(new ManualConfirmCheck(
      QStringLiteral("airframe.weight_balance"),
      QStringLiteral("Weight & Balance / CG"), CheckCategory::Airframe,
      QStringLiteral("Confirm CG within limits and payload secure"), {}, {}, this));
  m_checks.append(new ManualConfirmCheck(
      QStringLiteral("airframe.visual_inspection"),
      QStringLiteral("Visual Damage Inspection"), CheckCategory::Airframe,
      QStringLiteral("Confirm airframe free of cracks and damage"),
       {QStringLiteral("Antenna placement secure and unobstructed"),
        QStringLiteral("Airframe free of cracks, chips, or structural damage"),
        QStringLiteral("Fasteners, screws, and linkages tight"),
        QStringLiteral("Wiring and connectors secure"),
        QStringLiteral("Propeller/motor mounts undamaged")},
      {}, this));

  // Propulsion (1 remaining)
  m_checks.append(new ManualConfirmCheck(
      QStringLiteral("propulsion.propeller.direction"),
      QStringLiteral("Propeller Direction"), CheckCategory::Propulsion,
      QStringLiteral("Confirm propeller direction correct"),
      {}, {QStringLiteral("MOT_SPIN_DIRECTION"), QStringLiteral("FRAME_TYPE")},
      this));

  // Navigation (1)
  m_checks.append(new ManualConfirmCheck(
      QStringLiteral("sensors.compass.orientation"),
      QStringLiteral("Compass Orientation"), CheckCategory::Navigation,
      QStringLiteral("Confirm compass orientation setting matches installed direction"), {},
      {}, this));

  // Sort by category for deterministic order
  std::sort(m_checks.begin(), m_checks.end(),
            [](AbstractCheck *a, AbstractCheck *b) {
              if (a->category() != b->category())
                return a->categoryInt() < b->categoryInt();
              return a->id() < b->id();
            });

  // Deduplication: ensure no check ID appears more than once
  QSet<QString> seenIds;
  for (int i = 0; i < m_checks.size(); ++i) {
    if (seenIds.contains(m_checks[i]->id())) {
      qWarning() << "PreflightManager: removing duplicate check" << m_checks[i]->id();
      delete m_checks[i];
      m_checks.removeAt(i--);
    } else {
      seenIds.insert(m_checks[i]->id());
    }
  }

  // Nothing blocks arming until an airframe kind has been identified.
  // applyVehicleKind() re-marks the kind-relevant checks as mandatory when a
  // vehicle connects and its type is resolved (see updateCriticalChecks).
  for (auto *check : m_checks) {
    check->setMandatory(false);
  }

  for (auto *check : m_checks) {
    connectCheckSignals(check);
  }

  qDebug().noquote() << QStringLiteral("PreflightManager: %1 unique checks registered").arg(m_checks.size());

  emit modelChanged();
}

// Wire up signals for a check so that status changes trigger progress updates,
// database logging, and alert notifications.
void PreflightManager::connectCheckSignals(AbstractCheck *check) {
  connect(check, &AbstractCheck::statusChanged, this,
          [this, check](const QString &checkId, int newStatus) {
            Q_UNUSED(checkId)
            emit progressChanged();

            // Persist check result to DB for audit trail
            if (m_db && !checkId.isEmpty()) {
              QString deviceUid;
              if (m_telemetry && m_telemetry->vehicle()) {
                deviceUid = QString::number(m_telemetry->vehicle()->id());
              }
              QString statusStr;
              switch (static_cast<CheckStatus>(newStatus)) {
                case CheckStatus::Passed:  statusStr = QStringLiteral("Passed"); break;
                case CheckStatus::Failed:  statusStr = QStringLiteral("Failed"); break;
                case CheckStatus::Warning: statusStr = QStringLiteral("Warning"); break;
                case CheckStatus::Error:   statusStr = QStringLiteral("Error"); break;
                case CheckStatus::Skipped: statusStr = QStringLiteral("Skipped"); break;
                case CheckStatus::Stale:   statusStr = QStringLiteral("Stale"); break;
                default:                   statusStr = QStringLiteral("Pending"); break;
              }
              m_db->saveCheckResult(deviceUid, -1, checkId, statusStr,
                                    check->message());
            }
          });
  connect(check, &AbstractCheck::checkFailed, this,
          [this](const QString &checkId, const QString &reason) {
            emit checkFailed(m_sysId, checkId, reason);
            if (m_alertManager) {
              m_alertManager->postAlert(
                  QStringLiteral("preflight_check_failed"), 2,
                  QStringLiteral("preflight"),
                  QStringLiteral("Preflight Check Failed: %1").arg(reason));
            }
          });
  connect(check, &AbstractCheck::checkPassed, this, [this](const QString &) {
    emit progressChanged();
  });
  connect(check, &AbstractCheck::overrideLogged, this,
          [this](const QString &, const QString &, const QString &,
                 const QString &) { persistOverrides(); });
}

// Load persisted operator overrides from QSettings.
// Format: "status:reason" pairs keyed by check ID.
void PreflightManager::loadOverrides() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("preflight_overrides/%1").arg(m_sysId));
  QStringList keys = settings.childKeys();
  for (const QString &key : keys) {
    AbstractCheck *check = checkById(key);
    if (check) {
      QString json = settings.value(key).toString();
      // Minimal: store as "status:reason" pair
      QStringList parts = json.split(QStringLiteral(":"));
      if (parts.size() >= 1) {
        QString reason = parts.size() >= 2
                             ? parts.mid(1).join(QStringLiteral(":"))
                             : QString();
        check->overrideStatus(parts[0], reason);
      }
    }
  }
  settings.endGroup();
}

// Persist the latest override for each check to QSettings, plus any
// config overrides to the database. Called after every override event.
void PreflightManager::persistOverrides() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("preflight_overrides/%1").arg(m_sysId));
  for (auto *check : m_checks) {
    if (!check->overrideHistory().isEmpty()) {
      const auto &rec = check->overrideHistory().last();
      QString value = rec.newStatus + QStringLiteral(":") + rec.reason;
      settings.setValue(check->id(), value);
    }
    // Also persist check config overrides to DB
    if (m_db && !check->m_configCache.isEmpty()) {
      for (auto it = check->m_configCache.constBegin(); it != check->m_configCache.constEnd(); ++it) {
        m_db->setCheckConfig(check->id(), it.key(), it.value().toString());
      }
    }
  }
  settings.endGroup();
  settings.sync();
}
