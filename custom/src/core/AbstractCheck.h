/**
 * @file AbstractCheck.h
 * @brief Base class for all preflight checks in the QGC custom plugin.
 *
 * Defines the check lifecycle (Pending → Passed/Failed/etc.), status query interface,
 * operator override mechanism, and telemetry access helpers. Each concrete check
 * (e.g. BatteryVoltageCheck, GpsFixCheck) evaluates a single preflight precondition
 * and reports its result to the PreflightManager.
 *
 * Subclasses must implement evaluate() to perform the actual check logic using
 * values read from TelemetryBridge via the protected helper methods.
 */

#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>

class TelemetryBridge;

/// Evaluation status of a check, exposed to QML as int.
enum class CheckStatus : int {
    Pending  = 0,   ///< Not yet evaluated or waiting for data
    Passed   = 1,   ///< Check passed successfully
    Failed   = 2,   ///< Check failed (blocks arming if mandatory)
    Warning  = 3,   ///< Non-blocking warning condition detected
    Error    = 4,   ///< Error during evaluation (e.g. missing telemetry)
    Skipped  = 5,   ///< Check was skipped (e.g. hardware not present)
    Stale    = 6    ///< Data too old, requires re-evaluation
};

/// Categories grouping related checks for UI filtering and progress tracking.
enum class CheckCategory : int {
    Propulsion   = 0, ///< Motors, propellers, ESC
    Power,            ///< Battery, voltage, current
    Navigation,       ///< GPS, compass, home position
    Communication,    ///< RC link, telemetry link
    Airframe,         ///< Airframe configuration, sensors
    Safety,           ///< Failsafes, geofence, EKF
    Environment,      ///< Wind, temperature, terrain
    ArmingGate        ///< Gate-level arming decisions
};

/// Classification of check evaluation mode.
enum class CheckType : int {
    Auto     = 0, ///< Evaluated automatically on a timer
    Manual   = 1, ///< Requires user interaction to complete
    Action   = 2  ///< One-time user action (cannot be auto-overridden)
};

/// Record of a status override applied to a check.
/// Stored in the check's override history for audit trail and persistence.
struct OverrideRecord {
    QString checkId;
    QString previousStatus;
    QString newStatus;
    QString reason;
    QDateTime timestamp;
    QString operatorId;
};

/**
 * Abstract base class for a single preflight check.
 *
 * Lifecycle: constructed by PreflightManager, registered via addCheck(),
 * then periodically evaluated. The check reads telemetry values, evaluates
 * a condition, and calls setStatus() to report the result. Operators can
 * override non-Auto checks via overrideStatus()/confirm().
 */
class AbstractCheck : public QObject {
    Q_OBJECT
    /// Unique identifier for this check (e.g. "battery_voltage").
    Q_PROPERTY(QString checkId READ id CONSTANT)
    /// Human-readable display label.
    Q_PROPERTY(QString label READ label CONSTANT)
    /// Current evaluation status as integer (CheckStatus enum).
    Q_PROPERTY(int status READ statusInt NOTIFY statusChanged)
    /// Human-readable status text ("Passed", "Failed", etc.).
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    /// Detailed message describing the current status or failure reason.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    /// Current telemetry value relevant to this check.
    Q_PROPERTY(QVariant currentValue READ currentValue NOTIFY currentValueChanged)
    /// Current value formatted as a display string.
    Q_PROPERTY(QString currentValueString READ getCurrentValueString NOTIFY currentValueChanged)
    /// Whether this check is mandatory (blocks arming on failure).
    Q_PROPERTY(bool mandatory READ mandatory CONSTANT)
    /// Category index for grouping (CheckCategory enum).
    Q_PROPERTY(int checkCategory READ categoryInt CONSTANT)
    /// Type index (CheckType enum: Auto, Manual, Action).
    Q_PROPERTY(int type READ typeInt CONSTANT)
    /// Whether an override can be applied to this check.
    Q_PROPERTY(bool canOverride READ canOverride CONSTANT)
    /// Rationale explaining why this check exists.
    Q_PROPERTY(QString rationale READ getRationale CONSTANT)
    /// Step-by-step instructions to resolve a failure.
    Q_PROPERTY(QStringList fixSteps READ getFixSteps CONSTANT)
    /// Threshold/limit description for pass/fail criteria.
    Q_PROPERTY(QString threshold READ getThreshold CONSTANT)
    /// Whether this check belongs to the post-flight phase.
    Q_PROPERTY(bool isPostFlight READ isPostFlight CONSTANT)

public:
    /// Construct a new check.
    /// @param id        Unique string identifier (e.g. "battery_voltage")
    /// @param label     Display label for UI
    /// @param category  Functional category for grouping
    /// @param type      Evaluation mode (Auto / Manual / Action)
    /// @param mandatory Whether failure blocks arming
    /// @param canOverride Whether operator can override this check
    /// @param parent    QObject parent
    explicit AbstractCheck(const QString &id, const QString &label,
                           CheckCategory category, CheckType type,
                           bool mandatory = true, bool canOverride = true,
                           QObject *parent = nullptr);

    virtual ~AbstractCheck() = default;

    QString id() const { return m_id; }
    QString label() const { return m_label; }
    CheckStatus status() const { return m_status; }
    int statusInt() const { return static_cast<int>(m_status); }
    /// Human-readable status text for the current state.
    QString statusText() const;
    QString message() const { return m_message; }
    CheckCategory category() const { return m_category; }
    int categoryInt() const { return static_cast<int>(m_category); }
    CheckType checkType() const { return m_type; }
    int typeInt() const { return static_cast<int>(m_type); }
    bool mandatory() const { return m_mandatory; }
    // Auto checks cannot be overridden — only manual/action checks allow operator override.
    bool canOverride() const { return m_canOverride && m_type != CheckType::Auto; }
    QVariant currentValue() const { return m_currentValue; }

    QDateTime lastEvaluationTime() const { return m_lastEvalTime; }
    /// Whether the check data is stale and requires re-evaluation.
    bool requiresReevaluation() const;

    /// Run the check evaluation logic. Subclasses implement the actual test here.
    Q_INVOKABLE virtual void evaluate() = 0;

    /// User-facing message describing the check result.
    virtual QString getUserMessage() const;
    /// Recommended action for the operator to resolve a failure.
    virtual QString getRecommendedAction() const;
    /// Rationale explaining why this check exists.
    virtual QString getRationale() const;
    /// Step-by-step fix instructions for resolving a failure.
    virtual QStringList getFixSteps() const;
    /// Current value formatted as a human-readable string.
    virtual QString getCurrentValueString() const;
    /// Threshold or limit description for pass/fail criteria.
    virtual QString getThreshold() const;
    virtual void applyVehicleConfig(const QJsonObject &config);
    /// Whether this check blocks arming (same as mandatory).
    bool isBlocking() const { return m_mandatory; }
    /// Whether this check is evaluated automatically.
    bool isAuto() const { return m_type == CheckType::Auto; }
    /// Whether this check belongs to the post-flight phase.
    bool isPostFlight() const { return m_isPostFlight; }
    /// Mark this check as a post-flight check.
    void setIsPostFlight(bool v) { m_isPostFlight = v; }
    /// Override whether this check is mandatory (blocks arming on failure).
    void setMandatory(bool v) { m_mandatory = v; }

    /// Override the check status (e.g. force Passed).
    /// Returns true if the override was applied.
    Q_INVOKABLE bool overrideStatus(const QString &newStatus, const QString &reason = {});
    /// Confirm the check as passed (shortcut for overrideStatus("Passed")).
    /// Returns true if the confirmation was applied.
    Q_INVOKABLE bool confirm(const QString &reason = {});
    /// Reset the check to Pending status.
    Q_INVOKABLE virtual void reset();

    /// History of all status overrides applied to this check.
    QList<OverrideRecord> overrideHistory() const { return m_overrideHistory; }

signals:
    /// Emitted when the check status changes.
    void statusChanged(const QString &checkId, int newStatus);
    /// Emitted when the status detail message changes.
    void messageChanged(const QString &checkId, const QString &message);
    /// Emitted when the monitored telemetry value changes.
    void currentValueChanged(const QString &checkId, const QVariant &value);
    /// Emitted when the check passes evaluation.
    void checkPassed(const QString &checkId);
    /// Emitted when the check fails evaluation.
    void checkFailed(const QString &checkId, const QString &reason);
    /// Emitted when an override is logged for audit trail.
    void overrideLogged(const QString &checkId, const QString &previousStatus,
                        const QString &newStatus, const QString &reason);

protected:
    /// Update check status and emit appropriate signals. Called by subclasses in evaluate().
    void setStatus(CheckStatus newStatus, const QString &message = {});
    /// Update the displayed telemetry value and emit currentValueChanged.
    void setCurrentValue(const QVariant &value);
    /// Whether the vehicle is connected with acceptable signal quality (>= 10%).
    bool hasTelemetry() const;
    /// Whether a given property is available on the TelemetryBridge (includes param_ prefix fallback).
    bool isParamAvailable(const QString &prop) const;
    /// Read a double property from TelemetryBridge, falling back to dynamic properties and param_ prefix.
    double getTelemetryDouble(const QString &prop) const;
    /// Read a boolean property from TelemetryBridge.
    bool getTelemetryBool(const QString &prop) const;
    /// Read any QVariant property from TelemetryBridge.
    QVariant getTelemetryVariant(const QString &prop) const;

    /// Read a configurable double from the check_config table, falling back to defaultVal.
    double configDouble(const QString &key, double defaultVal) const;
    /// Read a configurable integer from the check_config table, falling back to defaultVal.
    int configInt(const QString &key, int defaultVal) const;
    /// Invalidate the config value cache (call when DB config changes).
    void clearConfigCache() const;

    friend class PreflightManager;
    friend class MissionEnergyCheckTest;

    // --- Core identity ---
    QString m_id;          ///< Unique string identifier (e.g. "battery_voltage")
    QString m_label;       ///< Human-readable display label
    CheckCategory m_category;
    CheckType m_type;

    // --- Runtime state ---
    CheckStatus m_status = CheckStatus::Pending;
    QString m_message;             ///< Detail message (failure reason, etc.)
    QVariant m_currentValue;       ///< Latest telemetry value being monitored
    bool m_mandatory = true;       ///< If true, failure blocks arming
    bool m_canOverride = true;     ///< If operator may override result
    bool m_isPostFlight = false;   ///< Whether this check belongs to the post-flight phase
    QDateTime m_lastEvalTime;      ///< Timestamp of last evaluate() call
    QList<OverrideRecord> m_overrideHistory; ///< Audit trail of overrides

    // --- Telemetry & config ---
    TelemetryBridge *m_telemetry = nullptr;
    mutable QHash<QString, QVariant> m_configCache; ///< Cached check_config values (mutable for const accessors)

private:
    /// Convert a status string ("passed", "failed", etc.) to the CheckStatus enum.
    CheckStatus statusFromString(const QString &s) const;

#ifdef QT_DEBUG
    friend class CheckSmokeTest;
    friend class RcCalibrationCheckTest;
    friend class WeatherProviderTest;
    QHash<QString, QVariant> m_testOverrides;
    Q_INVOKABLE void setTestValue(const QString &prop, const QVariant &val) {
        m_testOverrides[prop] = val;
    }
    Q_INVOKABLE void clearTestOverrides() {
        m_testOverrides.clear();
    }
#endif
};
