#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "utils/Config.h"

/// @file ArmingGate.h
/// Gate controller that evaluates preflight check results, enforces arming policy,
/// and manages operator overrides. Supports Passive, Active, and Hybrid modes.

class PreflightManager;
class TelemetryBridge;

class ArmingGate : public QObject {
    Q_OBJECT
    /// Whether arming is currently allowed.
    Q_PROPERTY(bool armingAllowed READ isArmingAllowed NOTIFY armingAllowedChanged)
    /// Human-readable reason why arming is denied.
    Q_PROPERTY(QString denialReason READ denialReason NOTIFY denialReasonChanged)
    /// Current gate mode (Passive / Active / Hybrid). Read-only from QML:
    /// Phase 1 fixes the gate at Active; the badge is display-only.
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    /// Whether an operator override is currently active.
    Q_PROPERTY(bool overrideActive READ isOverrideActive NOTIFY overrideActiveChanged)
    /// Which override is in force: none, a pending acknowledgment, or an
    /// open force-arm window. armingAllowed never reports an override as "open".
    Q_PROPERTY(OverrideKind overrideKind READ overrideKind NOTIFY overrideKindChanged)

public:
    /// Gate operating mode.
    /// Passive = log only, Active = block arming on failures, Hybrid = block + allow override.
    enum Mode { Passive = 0, Active, Hybrid };
    Q_ENUM(Mode)

    /// Override currently in force (see overrideKind).
    enum OverrideKind { NoOverride = 0, AckOverride, ForceOverride };
    Q_ENUM(OverrideKind)

    /// Policy for handling operator overrides.
    enum OverridePolicy {
        NONE = 0,          ///< No override allowed
        ALLOW_WITH_ACK     ///< Allow override with pilot acknowledgment
    };
    Q_ENUM(OverridePolicy)

    /// Records a pilot acknowledgment when overriding the gate.
    struct Acknowledgment {
        QString pilotName;   ///< Name of the pilot who acknowledged
        QString reason;      ///< Reason provided for the override
        QDateTime timestamp; ///< When the acknowledgment was made
    };

    /// Construct the arming gate controller.
    explicit ArmingGate(QObject *parent = nullptr);

    /// Set the preflight manager for check result access.
    void setPreflightManager(PreflightManager *manager);
    /// Set the telemetry bridge for vehicle state queries.
    void setTelemetryBridge(TelemetryBridge *bridge);

    bool isArmingAllowed() const { return m_armingAllowed; }
    bool isOverrideActive() const { return m_overrideActive; }
    QString denialReason() const { return m_denialReason; }
    Mode mode() const { return m_mode; }
    /// Set the gate operating mode.
    void setMode(Mode mode);
    OverridePolicy overridePolicy() const { return m_overridePolicy; }
    /// Set the override policy.
    void setOverridePolicy(OverridePolicy policy);

    /// Intercept a MAVLink COMMAND_LONG and block or allow it based on gate state.
    bool interceptCommandLong(uint16_t command, const QMap<int, float> &params);

    // ── One decision function ────────────────────────────────────────────
    /// Everything decide() needs about the vehicle and the checks.
    struct ArmRequest {
        bool isArm = true;                ///< false = disarm (always allowed)
        quint32 callToken = 0;            ///< token the arm command carries (0 = stock / query)
        qint64 heartbeatAgeMs = -1;       ///< since the last autopilot heartbeat, -1 = never
        bool allMandatoryPassed = false;
        QString blocker;                  ///< PreflightManager::armingBlocker()
        QStringList failingMandatoryIds;  ///< mandatory checks not Passed/Warning/Skipped
        bool anyMandatoryStale = false;
        bool training = false;
        bool batteryPackSelected = true;  ///< TODO(step3): battery-pack registry
    };
    /// Gate-owned state decide() reads.
    struct GateSnapshot {
        Mode mode = Active;
        OverridePolicy policy = ALLOW_WITH_ACK;
        bool forceWindowOpen = false;
        quint32 forceToken = 0;
        bool ackValid = false;
        quint32 ackToken = 0;
        QStringList ackCheckIds;          ///< failing checks the pilot acknowledged
    };
    struct ArmDecision {
        enum Verdict { Allow = 0, NeedsAck, Deny };
        enum Kind { Normal = 0, Ack, Force };
        Verdict verdict = Deny;
        Kind kind = Normal;
        QString reason;
        QStringList failingIds;
    };
    /// The single arm decision, pure. Rules in order:
    ///  1. disarm → Allow;  2. Training → Deny;
    ///  3. force window open → Allow (Force) only for the minted token, else Deny;
    ///  4. Passive mode (tests only; QML cannot set it) → Allow;
    ///  5. hard blocks → Deny: no / stale autopilot heartbeat, any mandatory
    ///     check Stale, no battery pack;
    ///  6. all mandatory checks pass → Allow;
    ///  7. ALLOW_WITH_ACK: a valid ack whose token the call carries and that
    ///     names every currently failing check → Allow (Ack); a new failure
    ///     since the ack → Deny; otherwise NeedsAck;
    ///  8. else Deny.
    static ArmDecision decide(const ArmRequest &request, const GateSnapshot &gate);
    /// Live inputs for decide().
    ArmRequest currentRequest(quint32 callToken, bool isArm = true) const;
    GateSnapshot snapshot() const;
    /// Every QML Arm button calls this. Allow → sends the arm via the
    /// telemetry bridge; Deny → emits commandRejected(reason). Returns
    /// {verdict: "allow"|"needsAck"|"deny", kind, reason, failing}.
    Q_INVOKABLE QVariantMap requestArm();
    /// What requestArm() would decide now, without sending or emitting.
    Q_INVOKABLE QVariantMap previewArm() const;
    OverrideKind overrideKind() const;

    /// Acknowledge override with pilot name and reason (one-time bypass).
    Q_INVOKABLE bool acknowledgeOverride(const QString &pilotName, const QString &reason);
    /// Test-only: set a custom override duration in milliseconds for testing override
    /// expiry. Mints and returns a fresh one-shot caller token like forceArm().
    quint32 setOverrideDuration(int timeoutMs);
    /// Test-only: override the ALLOW_WITH_ACK unused-acknowledgment expiry window.
    /// Production uses kAckExpirySec (see Config.h); tests set a short window here
    /// so expiry can be exercised without waiting the operational timeout.
    void setAckExpiryMs(int ms) { if (ms > 0) m_ackExpiryMs = ms; }
    /// Reset the gate to normal evaluation mode.
    Q_INVOKABLE void resetGate();
    /// Force arming regardless of gate state. Opens a bounded, logged window and
    /// mints a one-shot CALLER-SCOPED token that it returns: the holder attaches
    /// the token to its SPECIFIC pending arm command (Vehicle::setArmOverrideToken),
    /// and the interceptor authorizes exactly that command, consuming the token.
    /// The window is NOT ambient — no other arm passes during it. Auto-closes
    /// after kForceArmOverrideSec even if the token was never presented.
    Q_INVOKABLE quint32 forceArm();
    Q_INVOKABLE quint32 ackTokenForArm() const {
        if (m_ackReceived) return m_ackToken;
        return 0;
    }

    /// Build a vehicle-level arm interceptor consulting this gate. The returned
    /// callable takes a QString& denial-reason slot plus the caller-scoped arm
    /// token the vehicle currently bears, and returns true when arming is
    /// allowed or an override is active; false otherwise (fail-closed when the
    /// gate is gone). During a force-arm override only the command presenting
    /// the current unconsumed minted token passes; there is no ambient window.
    /// Installed via Vehicle::setArmCommandInterceptor.
    std::function<bool(QString&, quint32)> makeArmInterceptor();

    /// Most recent pilot acknowledgment record.
    Acknowledgment lastAcknowledgment() const { return m_lastAck; }

signals:
    /// Emitted when arming permission changes.
    void armingAllowedChanged(bool allowed);
    /// Emitted when the denial reason changes.
    void denialReasonChanged(const QString &reason);
    /// Emitted when a MAVLink arm command is denied.
    void armingDenied(uint16_t command, const QString &reason);
    /// Emitted when an override is activated.
    void armingOverrideActivated(const QString &reason, int timeoutSec);
    /// Emitted when a timed override expires.
    void armingOverrideExpired();
    /// Emitted when the override active state changes.
    void overrideActiveChanged(bool active);
    /// Emitted when the gate mode changes.
    void modeChanged(Mode mode);

    /// Emitted when the gate opens (arming permitted).
    void gateOpened();
    /// Emitted when the gate closes (arming blocked).
    void gateClosed(const QString &reason);
    /// Emitted when the pilot acknowledges an override.
    void overrideAcknowledged(const QString &pilotName, const QString &reason);

    /// Emitted to log a gate-level override for audit trail.
    void gateOverrideLogged(const QString &reason, int timeoutSec);

    /// Emitted when the vehicle rejects a command (arm, takeoff, mode change).
    /// QML binds to this to show a toast/banner with the rejection reason.
    void commandRejected(const QString &reason);

    /// Emitted when the vehicle confirms it has armed.
    void vehicleArmed();
    /// Emitted when the vehicle confirms it has disarmed.
    void vehicleDisarmed();
    /// Emitted when overrideKind changes.
    void overrideKindChanged();

private slots:
    void onCheckFailed(const QString &checkId, const QString &reason);
    void onAllChecksPassed();

private:
    void updateArmingState();
    /// Spend the acknowledgment (it authorized one arm) and log it.
    void consumeAck();
    /// Spend the force token and close the force window (1.3: close on use).
    void consumeForceToken();
    static QVariantMap toVariantMap(const ArmDecision &decision);

    PreflightManager *m_manager = nullptr;
    TelemetryBridge *m_telemetry = nullptr;
    qint64 m_lastTickMs = -1;   ///< MonotonicClock ms of the last autopilot heartbeatTick, -1 = never
    Mode m_mode = Active;
    OverridePolicy m_overridePolicy = ALLOW_WITH_ACK;
    bool m_armingAllowed = false;
    bool m_overrideActive = false;
    bool m_ackReceived = false;
    QString m_denialReason;
    QTimer *m_overrideTimer = nullptr;
    QTimer *m_gateTimer = nullptr;
    QTimer *m_ackTimer = nullptr;
    Acknowledgment m_lastAck;
    quint32 m_ackToken = 0;
    QStringList m_ackCheckIds;   ///< failing mandatory checks when the pilot acknowledged
    int m_ackExpiryMs = kAckExpirySec * 1000;

    // One-shot override token model. forceArm()/setOverrideDuration() mint a fresh
    // token (m_overrideTokenSeq counter) and return it to the caller, who attaches
    // it to the vehicle for its specific pending command. The interceptor
    // authorizes exactly the command presenting the CURRENT unconsumed token and
    // consumes it on success. m_forceArmOverride marks the window as
    // caller-scoped. A force-arm window whose token is spent or that expires
    // authorizes no other command: it is not ambient.
    quint32 m_overrideToken = 0;
    quint32 m_overrideTokenSeq = 0;   ///< never reset, so tokens never repeat across windows
    bool m_forceArmOverride = false;

    uint16_t m_armCommandCode = kMavCmdArmDisarm;
    static constexpr int EVAL_INTERVAL_MS = kArmingGateEvalMs;
};
