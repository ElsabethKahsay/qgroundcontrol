#pragma once
#include <QObject>
#include <QVector>
#include <QTimer>
#include <QQmlListProperty>

#include "AbstractCheck.h"
#include "AutopilotInfoDetector.h"
#include "PreflightStateMachine.h"
#include "UavParameterManager.h"

/**
 * @file PreflightManager.h
 * @brief Central manager that owns, evaluates, and tracks all preflight checks.
 *
 * One PreflightManager exists per vehicle. It:
 *   - Registers ~40 AbstractCheck instances at construction time
 *   - Runs periodic evaluation via a QTimer
 *   - Tracks pass/fail/pending/stale progress counters for QML binding
 *   - Drives the PreflightStateMachine through its lifecycle
 *   - Persists operator overrides to QSettings
 *   - Logs check results to DatabaseManager for audit trail
 *
 * QML accesses checks, progress, and arming status through Q_PROPERTY bindings.
 */

class TelemetryBridge;
class AlertManager;
class DatabaseManager;

/**
 * Manages all preflight checks for a single UAV vehicle.
 *
 * Instantiated when a vehicle connects; destroyed on disconnect.
 * Owns the list of AbstractCheck objects and a PreflightStateMachine
 * that tracks the progression from Disconnected to ArmingAllowed.
 */
class PreflightManager : public QObject {
    Q_OBJECT
    /// Total number of registered checks.
    Q_PROPERTY(int totalChecks READ totalChecks NOTIFY modelChanged)
    /// Number of checks that passed.
    Q_PROPERTY(int passedChecks READ passedChecks NOTIFY progressChanged)
    /// Number of checks that failed.
    Q_PROPERTY(int failedChecks READ failedChecks NOTIFY progressChanged)
    /// Number of checks still pending evaluation.
    Q_PROPERTY(int pendingChecks READ pendingChecks NOTIFY progressChanged)
    /// Number of checks with stale (outdated) data.
    Q_PROPERTY(int staleCount READ staleCount NOTIFY progressChanged)
    /// Number of mandatory checks that are currently failing.
    Q_PROPERTY(int blockingFailedCount READ blockingFailedCount NOTIFY progressChanged)
    /// Description of the first blocking failure encountered.
    Q_PROPERTY(QString firstBlockingFailure READ firstBlockingFailure NOTIFY progressChanged)
    /// Completion percentage (0-100) based on passed vs total.
    Q_PROPERTY(int completionPercent READ completionPercent NOTIFY progressChanged)
    /// Whether all mandatory checks have passed.
    Q_PROPERTY(bool allMandatoryPassed READ allMandatoryPassed NOTIFY progressChanged)
    /// Overall progress as a fraction (0.0 - 1.0).
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    /// Current reason arming is blocked (empty if arming allowed).
    Q_PROPERTY(QString armingBlocker READ armingBlocker NOTIFY progressChanged)
    /// Whether periodic evaluation is active.
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    /// List of all registered checks (QML-accessible).
    Q_PROPERTY(QQmlListProperty<AbstractCheck> checks READ qmlChecks NOTIFY modelChanged)
    /// List of checks that are currently blocking arming.
    Q_PROPERTY(QVariantList blockingChecks READ blockingChecks NOTIFY modelChanged)
    /// Number of checks currently blocking arming.
    Q_PROPERTY(int blockingCount READ blockingCount NOTIFY modelChanged)
    /// Current state machine state.
    Q_PROPERTY(PreflightStateMachine::State state READ state NOTIFY stateChanged)

public:
    /// Construct a preflight manager for the given vehicle system ID.
    explicit PreflightManager(uint8_t vehicleSysId = 1, QObject *parent = nullptr);
    ~PreflightManager() override;

    /// Set the telemetry bridge for accessing vehicle data.
    void setTelemetryBridge(TelemetryBridge *bridge);
    /// Set the alert manager for user-facing notifications.
    void setAlertManager(AlertManager *alertManager);
    /// Set the database manager for persisting overrides.
    void setDatabaseManager(DatabaseManager *db);

    uint8_t vehicleSysId() const { return m_sysId; }

    QVector<AbstractCheck *> checks() const { return m_checks; }
    /// QML-accessible list property for all registered checks.
    QQmlListProperty<AbstractCheck> qmlChecks();
    static AbstractCheck *qmlChecksAt(QQmlListProperty<AbstractCheck> *list, qsizetype index);
    static qsizetype qmlChecksCount(QQmlListProperty<AbstractCheck> *list);

    /// Find a check by its unique identifier.
    Q_INVOKABLE AbstractCheck *checkById(const QString &checkId) const;

    /// Number of checks in the given category.
    Q_INVOKABLE int checksInCategory(int categoryId) const;
    /// Number of passed checks in the given category.
    Q_INVOKABLE int passedInCategory(int categoryId) const;
    /// Whether the given category has any registered checks.
    Q_INVOKABLE bool hasCategory(int categoryId) const;
    /// Return checks matching the given list of category IDs.
    Q_INVOKABLE QObjectList checksForCategory(const QVariantList &categories) const;
    /// List of checks currently blocking arming.
    QVariantList blockingChecks() const;
    /// Number of checks currently blocking arming.
    int blockingCount() const;

    int totalChecks() const { return m_checks.size(); }
    /// Count of checks with Passed status.
    int passedChecks() const;
    /// Count of checks with Failed status.
    int failedChecks() const;
    /// Count of checks with Pending status.
    int pendingChecks() const;
    /// Count of mandatory checks that are currently failing.
    int blockingFailedCount() const;
    /// Description of the first blocking failure.
    QString firstBlockingFailure() const;
    /// Number of checks with stale data.
    int staleCount() const;
    /// Completion percentage (0-100).
    int completionPercent() const;
    /// Whether all mandatory checks have passed.
    bool allMandatoryPassed() const;
    /// Overall progress as a fraction 0.0-1.0.
    double progress() const;
    /// Current arming blocker reason (empty if none).
    QString armingBlocker() const;
    bool isActive() const { return m_active; }
    PreflightStateMachine::State state() const { return m_stateMachine.state(); }

    /// Staleness threshold in milliseconds (default 10000 ms).
    int stalenessThresholdMs() const { return m_stalenessThresholdMs; }
    /// Set the staleness threshold in milliseconds.
    void setStalenessThresholdMs(int ms) { m_stalenessThresholdMs = ms; }

    /// Load persisted overrides from the database.
    void loadOverrides();
    /// Persist current overrides to the database.
    void persistOverrides();
    /// Register a new check for evaluation.
    void addCheck(AbstractCheck *check);

public slots:
    /// Start periodic evaluation at the given interval (ms).
    void startEvaluation(int intervalMs = 1000);
    /// Stop periodic evaluation.
    void stopEvaluation();
    /// Immediately evaluate all checks.
    void evaluateAll();
    /// Reset all checks to Pending status.
    void resetAll();
    /// Activate post-flight checks: skip non-post-flight, reset post-flight ones.
    Q_INVOKABLE void activatePostFlightChecks();
    /// Apply vehicle-kind-aware blocking rules ("MULTIROTOR", "FIXED_WING", ...).
    /// Called when the vehicle type resolves so arming never blocks on a check
    /// that cannot apply to the connected airframe.
    Q_INVOKABLE void applyVehicleKind(const QString &kindString);
    /// Enable or disable periodic evaluation.
    void setActive(bool active);
    /// Timer tick — evaluates stale checks and drives state machine.
    void tick();

signals:
    /// Emitted when the check list model changes (add/remove).
    void modelChanged();
    /// Emitted when any progress counter changes.
    void progressChanged();
    /// Emitted when all checks have passed and arming is allowed.
    void allChecksPassed(uint8_t vehicleSysId);
    /// Emitted when arming is blocked due to check failures.
    void armingBlocked(uint8_t vehicleSysId, const QString &reason);
    /// Emitted when an individual check fails.
    void checkFailed(uint8_t vehicleSysId, const QString &checkId, const QString &reason);
    /// Emitted when the active state changes.
    void activeChanged(bool active);
    /// Emitted when the arming blocker reason changes.
    void armingBlockerChanged(const QString &reason);
    /// Emitted when the state machine transitions.
    void stateChanged();

private:
    /// Attempt to advance or retreat the state machine based on current check results.
    void attemptStateTransition();
    /// Create and register all ~40 built-in preflight checks.
    void createPhase1Checks();
    /// Apply vehicle-type-aware critical (mandatory) check set for the given kind.
    void updateCriticalChecks(const QString &kindString);
    /// Wire up statusChanged/checkFailed/checkPassed signals for a check.
    void connectCheckSignals(AbstractCheck *check);
    /// Pull initial parameter values from QGC's ParameterManager into UavParameterManager.
    void _initialParamRefresh();

    uint8_t m_sysId;
    TelemetryBridge *m_telemetry = nullptr;
    AlertManager *m_alertManager = nullptr;
    DatabaseManager *m_db = nullptr;
    QVector<AbstractCheck *> m_checks;
    QTimer *m_timer = nullptr;
    bool m_active = false;
    int m_evalIntervalMs = 1000;
    QString m_lastBlocker;
    PreflightStateMachine m_stateMachine;
    UavParameterManager m_paramManager;
    AutopilotInfoDetector m_autopilotDetector;
    int m_stalenessThresholdMs = 10000;
};
