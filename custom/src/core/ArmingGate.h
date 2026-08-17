#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>

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
    /// Current gate mode (Passive / Active / Hybrid).
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    /// Whether an operator override is currently active.
    Q_PROPERTY(bool overrideActive READ isOverrideActive NOTIFY overrideActiveChanged)

public:
    /// Gate operating mode.
    /// Passive = log only, Active = block arming on failures, Hybrid = block + allow override.
    enum Mode { Passive = 0, Active, Hybrid };
    Q_ENUM(Mode)

    /// Result of processing an arm request.
    enum GateDecision { GATE_OPEN = 0, GATE_CLOSED };
    Q_ENUM(GateDecision)

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

    /// Evaluate arm request against current check failures.
    /// Returns GATE_OPEN or GATE_CLOSED with reason.
    GateDecision processArmRequest(int criticalFailCount, int manualFailCount);

    /// Acknowledge override with pilot name and reason (one-time bypass).
    Q_INVOKABLE bool acknowledgeOverride(const QString &pilotName, const QString &reason);
    /// Temporarily override the gate for a given duration.
    Q_INVOKABLE void overrideGate(const QString &reason, int timeoutSec = 30);
    /// Reset the gate to normal evaluation mode.
    Q_INVOKABLE void resetGate();
    /// Force arming regardless of gate state.
    Q_INVOKABLE void forceArm();

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

    /// Emitted when the vehicle confirms it has armed.
    void vehicleArmed();
    /// Emitted when the vehicle confirms it has disarmed.
    void vehicleDisarmed();

private slots:
    void onCheckFailed(const QString &checkId, const QString &reason);
    void onAllChecksPassed();

private:
    void updateArmingState();
    bool evaluateGate();
    QString buildDenialReason(int criticalFailCount, int manualFailCount) const;

    PreflightManager *m_manager = nullptr;
    TelemetryBridge *m_telemetry = nullptr;
    QDateTime m_lastTickTime;
    Mode m_mode = Active;
    OverridePolicy m_overridePolicy = ALLOW_WITH_ACK;
    bool m_armingAllowed = false;
    bool m_overrideActive = false;
    bool m_ackReceived = false;
    QString m_denialReason;
    QTimer *m_overrideTimer = nullptr;
    QTimer *m_gateTimer = nullptr;
    Acknowledgment m_lastAck;

    uint16_t m_armCommandCode = kMavCmdArmDisarm;
    static constexpr int EVAL_INTERVAL_MS = kArmingGateEvalMs;
};
