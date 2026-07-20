#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>

class TelemetryBridge;

/// @file AbstractCheck.h
/// Base class for all preflight checks. Defines the check lifecycle, status query interface,
/// override mechanism, and telemetry access helpers. Each concrete check evaluates a single
/// precondition and reports its result to the PreflightManager.

/// Current evaluation status of a check.
/// Maps to integer values exposed to QML.
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
    void setStatus(CheckStatus newStatus, const QString &message = {});
    void setCurrentValue(const QVariant &value);
    bool hasTelemetry() const;
    bool isParamAvailable(const QString &prop) const;
    double getTelemetryDouble(const QString &prop) const;
    bool getTelemetryBool(const QString &prop) const;
    QVariant getTelemetryVariant(const QString &prop) const;

    /// Read a configurable double from the check_config table, falling back to defaultVal.
    double configDouble(const QString &key, double defaultVal) const;
    /// Read a configurable integer from the check_config table, falling back to defaultVal.
    int configInt(const QString &key, int defaultVal) const;

    friend class PreflightManager;
    friend class MissionEnergyCheckTest;

    QString m_id;
    QString m_label;
    CheckCategory m_category;
    CheckType m_type;
    CheckStatus m_status = CheckStatus::Pending;
    QString m_message;
    QVariant m_currentValue;
    bool m_mandatory = true;
    bool m_canOverride = true;
    QDateTime m_lastEvalTime;
    QList<OverrideRecord> m_overrideHistory;
    TelemetryBridge *m_telemetry = nullptr;

private:
    CheckStatus statusFromString(const QString &s) const;

#ifdef QT_DEBUG
    friend class CheckSmokeTest;
    friend class RcCalibrationCheckTest;
    friend class CompassOrientationCheckTest;
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
