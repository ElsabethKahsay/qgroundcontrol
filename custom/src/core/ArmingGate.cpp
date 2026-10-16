/**
 * @file ArmingGate.cpp
 * @brief Gate controller that decides whether to allow or block arming.
 *
 * Evaluates preflight check results against the configured gate mode
 * (Passive/Active/Hybrid), enforces operator override policy, and
 * intercepts MAVLink arm commands. Emits signals on state changes
 * so the UI can reflect arming permission in real time.
 */

#include "ArmingGate.h"

#include <QDebug>
#include <QTimer>

#include "AbstractCheck.h"
#include "PreflightManager.h"
#include "TelemetryBridge.h"
#include "Vehicle/Vehicle.h"
#include "utils/Config.h"
#include "utils/DatabaseManager.h"
#include "FlightSession.h"

// Set up the override expiration timer and the periodic gate evaluation timer.
ArmingGate::ArmingGate(QObject *parent)
    : QObject(parent)
{
    // Override timer: fires once after timeoutSec to deactivate a temporary override
    m_overrideTimer = new QTimer(this);
    m_overrideTimer->setSingleShot(true);
    connect(m_overrideTimer, &QTimer::timeout, this, [this]() {
        m_overrideActive = false;
        m_overrideToken = 0;
        m_forceArmOverride = false;
        emit overrideActiveChanged(false);
        emit armingOverrideExpired();
        // Re-evaluate checks now that the override window is closed; if checks
        // are failing the gate re-closes immediately instead of waiting for the
        // next periodic tick.
        updateArmingState();
    });

    // Gate timer: periodically re-evaluates arming state at a fixed interval
    m_gateTimer = new QTimer(this);
    m_gateTimer->setInterval(EVAL_INTERVAL_MS);
    connect(m_gateTimer, &QTimer::timeout, this, &ArmingGate::updateArmingState);

    // After any disarm, close the gate so the next arm request is re-checked.
    // Without this, a prior force-arm/override would let arm commands through
    // indefinitely regardless of current check results.
    connect(this, &ArmingGate::vehicleDisarmed, this, [this]() {
        if (m_overrideActive || m_ackReceived) {
            resetGate();
        }
    });
}

// Wire up to PreflightManager so the gate reacts to check failures and passes.
// Disconnects any previous manager first to avoid duplicate connections.
void ArmingGate::setPreflightManager(PreflightManager *manager)
{
    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
    }
    m_manager = manager;
    if (m_manager) {
        connect(m_manager, &PreflightManager::checkFailed,
                this, [this](uint8_t, const QString &checkId, const QString &reason) {
                    onCheckFailed(checkId, reason);
                });
        connect(m_manager, &PreflightManager::allChecksPassed,
                this, [this](uint8_t) {
                    onAllChecksPassed();
                });
        // NOTE: the gate's telemetry-freshness clock is NOT fed from
        // PreflightManager::progressChanged. That signal fires on check-status
        // changes / evaluation ticks, so it stays alive on a dead link (the
        // manager timer keeps ticking) and it falsely expires on a live link
        // whose checks are simply stable. The clock is fed by actual telemetry
        // arrival instead — see setTelemetryBridge().
        connect(this, &ArmingGate::gateOverrideLogged,
                m_manager, [this](const QString &reason, int) {
                    Q_UNUSED(reason)
                    m_manager->persistOverrides();
                });
    }
    if (!m_gateTimer->isActive())
        m_gateTimer->start();
    updateArmingState();
}

// Store the telemetry bridge and drive the freshness clock from real telemetry
// arrival. Every received HEARTBEAT stamps m_lastTickTime, so:
//   * a live link keeps the gate fresh regardless of whether any check status
//     changed (no progressChanged required), and
//   * a dead link stops stamping, so the gate closes with a staleness reason
//     after kTelemetryStalenessSec.
void ArmingGate::setTelemetryBridge(TelemetryBridge *bridge)
{
    if (m_telemetry == bridge)
        return;
    if (m_telemetry)
        disconnect(m_telemetry, nullptr, this, nullptr);
    m_telemetry = bridge;
    if (m_telemetry) {
        connect(m_telemetry, &TelemetryBridge::heartbeatTick, this, [this]() {
            m_lastTickTime = QDateTime::currentDateTime();
            updateArmingState();
        });
    }
    updateArmingState();
}

// Update the gate operating mode (Passive/Active/Hybrid) and re-evaluate.
void ArmingGate::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    emit modeChanged(mode);
    updateArmingState();
}

// Intercept a MAVLink COMMAND_LONG for arm/disarm.
// Returns true to allow the command through, false to block it.
// Only the arm command (param1=1) is intercepted; disarm passes through.
bool ArmingGate::interceptCommandLong(uint16_t command, const QMap<int, float> &params)
{
    float armParam = params.value(1, 0.0f);
    bool isArm = qFuzzyCompare(armParam, 1.0f);
    if (!isArm)
        return true;

    if (m_overrideActive)
        return true;

    // Training mode hard block — cannot be overridden unless force arm / gate override is active
    FlightSession *session = FlightSession::instance();
    if (session && session->isTraining()) {
        qWarning().noquote() << QStringLiteral("ArmingGate: Arm blocked — training mode active");
        m_denialReason = QStringLiteral("Arming is disabled in training mode");
        m_armingAllowed = false;
        emit armingAllowedChanged(false);
        emit denialReasonChanged(m_denialReason);
        emit armingDenied(command, m_denialReason);
        return false;
    }

    if (m_ackReceived) {
        m_ackReceived = false;
        qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK bypass for pilot %1")
                                    .arg(m_lastAck.pilotName);
        m_denialReason.clear();
        m_armingAllowed = true;
        emit armingAllowedChanged(true);
        emit denialReasonChanged({});
        return true;
    }

    if (m_mode == Passive)
        return true;

    if (!m_manager) {
        qWarning() << "ArmingGate: no PreflightManager set";
        return true;
    }

    bool allPassed = m_manager->allMandatoryPassed();

    if ((m_mode == Hybrid || m_mode == Active) && !allPassed) {
        QString reason = m_manager->armingBlocker();
        if (reason.isEmpty())
            reason = QStringLiteral("Preflight checks not complete");
        m_denialReason = reason;
        m_armingAllowed = false;
        emit armingAllowedChanged(false);
        emit denialReasonChanged(reason);
        emit armingDenied(command, reason);
        qWarning().noquote() << QStringLiteral("Arming blocked: %1").arg(reason);
        return false;
    }

    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});
    return true;
}

// Evaluate an arm request against current check failure counts.
// In Passive mode or with an active override, the gate is always open.
// In Active/Hybrid mode, any critical failure closes the gate.
ArmingGate::GateDecision ArmingGate::processArmRequest(int criticalFailCount, int manualFailCount)
{
    if (m_overrideActive || m_mode == Passive)
        return GATE_OPEN;

    // ALLOW_WITH_ACK: if ack received, allow one-time bypass
    if (m_overridePolicy == ALLOW_WITH_ACK && m_ackReceived &&
        (criticalFailCount > 0 || manualFailCount > 0)) {
        m_ackReceived = false;
        qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK bypass for pilot %1")
                                    .arg(m_lastAck.pilotName);
        return GATE_OPEN;
    }

    // ACTIVE or HYBRID + any blocking fail = suppress
    if ((m_mode == Active || m_mode == Hybrid) && criticalFailCount > 0) {
        QString reason = buildDenialReason(criticalFailCount, manualFailCount);
        m_denialReason = reason;
        m_armingAllowed = false;
        emit armingAllowedChanged(false);
        emit denialReasonChanged(reason);
        emit gateClosed(reason);
        return GATE_CLOSED;
    }

    // Allow pass-through
    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});
    emit gateOpened();
    return GATE_OPEN;
    
}

// Record a pilot's acknowledgment of override responsibility.
// Requires a non-empty pilot name; the acknowledgment is one-time use.
bool ArmingGate::acknowledgeOverride(const QString &pilotName, const QString &reason)
{
    if (pilotName.trimmed().isEmpty())
        return false;

    m_ackReceived = true;
    m_lastAck.pilotName = pilotName.trimmed();
    m_lastAck.reason = reason;
    m_lastAck.timestamp = QDateTime::currentDateTime();

    qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK acknowledged by %1 — %2")
                                .arg(pilotName, reason);
    emit overrideAcknowledged(pilotName, reason);
    return true;
}

void ArmingGate::setOverridePolicy(OverridePolicy policy)
{
    m_overridePolicy = policy;
}

// Temporarily override the gate, allowing arming for a limited duration.
// Logs the override for audit trail and emits signals for UI notification.
void ArmingGate::overrideGate(const QString &reason, int timeoutSec)
{
    m_overrideActive = true;
    m_overrideTimer->start(timeoutSec * 1000);
    emit overrideActiveChanged(true);
    emit armingOverrideActivated(reason, timeoutSec);

    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});

    emit gateOverrideLogged(reason, timeoutSec);

    qWarning().noquote() << QStringLiteral("Arming overridden: %1 (timeout %2s)")
                                .arg(reason).arg(timeoutSec);
}

// Reset the gate to normal evaluation mode, clearing any active override.
void ArmingGate::resetGate()
{
    m_overrideActive = false;
    m_ackReceived = false;
    m_overrideTimer->stop();
    m_overrideToken = 0;
    m_overrideTokenSeq = 0;
    m_forceArmOverride = false;
    emit overrideActiveChanged(false);
    updateArmingState();
}

// Unconditionally allow the SPECIFIC pending command through a bounded window.
// Test-only: set a custom override duration in milliseconds for testing override
// expiration. Mints a fresh one-shot caller token (like forceArm()) so the
// window stays caller-scoped; the gate re-closes after the given window exactly
// as the timed force-arm path does.
quint32 ArmingGate::setOverrideDuration(int timeoutMs)
{
    m_overrideActive = true;
    emit overrideActiveChanged(true);
    emit armingOverrideActivated(QStringLiteral("Test override"), timeoutMs);

    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});

    // Mint a one-shot caller-scoped token for the sanctioned caller.
    m_forceArmOverride = true;
    const quint32 token = ++m_overrideTokenSeq;
    m_overrideToken = token;

    // Close the window automatically so the gate does not stay open forever.
    m_overrideTimer->start(timeoutMs);
    return token;
}

// Unconditionally authorise the SANCTIONED caller's pending command, once.
// Force-arm path: opens a bounded, logged window and mints a one-shot
// CALLER-SCOPED token returned to the caller. The caller (TelemetryBridge)
// attaches the token to its vehicle so the interceptor authorizes exactly that
// pending command — and nothing else while the window is open. The window is
// NOT ambient: it auto-closes after kForceArmOverrideSec even if the token was
// never presented (60s hard expiry on an unconsumed token).
quint32 ArmingGate::forceArm()
{
    m_overrideActive = true;
    emit overrideActiveChanged(true);
    emit armingOverrideActivated(QStringLiteral("Operator force arm"), kForceArmOverrideSec);

    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});

    // Mint a one-shot caller-scoped token for the pending command about to be
    // issued by the sanctioned caller.
    m_forceArmOverride = true;
    const quint32 token = ++m_overrideTokenSeq;
    m_overrideToken = token;

    emit gateOverrideLogged(QStringLiteral("Operator force arm"), kForceArmOverrideSec);
    qWarning().noquote() << QStringLiteral("Arming overridden: Operator force arm (%1s window)").arg(kForceArmOverrideSec);

    // Close the window automatically so the gate does not stay open forever.
    m_overrideTimer->start(kForceArmOverrideSec * 1000);
    return token;
}

// Build a vehicle-level arm interceptor that consults this gate. The returned
// callable allows arming only when the gate allows it or the OVERRIDE KIND
// genuinely covers the call; it fills the denial reason otherwise and fails
// closed if the gate has been destroyed. The Vehicle stores no reference to the
// gate itself, only to this callable, so no extra coupling is introduced.
//
// Override kinds:
//   * force-arm override (forceArm()/setOverrideDuration()): CALLER-SCOPED and
//     ONE-SHOT. Only the command presenting the current unconsumed minted token
//     passes, and it is consumed on success. An open force-arm window whose token
//     is spent or never presented authorizes NOTHING else until its exhaust —
//     this window is not ambient.
//   * acknowledged override (acknowledgeOverride(), ArmGateDialog.qml): ONE-SHOT.
//     The first arm to reach this interceptor is authorized and consumes the
//     acknowledgment. This is the LOCAL UI arm path (TelemetryBridge::arm());
//     interceptCommandLong() only sees INBOUND peer commands, so honoring the ack
//     there alone left the dialog's "Acknowledge & Arm" vetoed by the very checks
//     the operator had just acknowledged.
//   * gate override (overrideGate()): permissive for the window by design.
std::function<bool(QString&, quint32)> ArmingGate::makeArmInterceptor() const
{
    QPointer<const ArmingGate> guard(this);
    return [guard](QString &denialReason, quint32 callToken) -> bool {
        if (!guard) {
            denialReason = QStringLiteral("Arming gate unavailable (fail-closed)");
            return false;
        }
        if (guard->isOverrideActive() && guard->m_forceArmOverride) {
            // One-shot, caller-scoped force-arm window: authorize exactly the
            // command holding the current unconsumed token, then consume it.
            // Any other arm inside the window is denied — including after this
            // token is spent (it stays exclusive until the window closes).
            if (guard->m_overrideToken != 0 && callToken == guard->m_overrideToken) {
                const_cast<ArmingGate*>(guard.data())->m_overrideToken = 0;
                denialReason.clear();
                return true;
            }
            denialReason = QStringLiteral("Force-arm override is one-shot and caller-scoped; "
                                          "this arm command does not hold the minted token");
            return false;
        }
        // Acknowledged override (ALLOW_WITH_ACK): authorize exactly one arm, then
        // consume the acknowledgment. Guarded by the policy so a NONE policy
        // cannot be bypassed by a stray acknowledgment.
        if (guard->m_overridePolicy == OverridePolicy::ALLOW_WITH_ACK && guard->m_ackReceived) {
            ArmingGate *that = const_cast<ArmingGate*>(guard.data());
            that->m_ackReceived = false;
            that->m_denialReason.clear();
            denialReason.clear();
            qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK bypass consumed by pilot %1")
                                        .arg(that->m_lastAck.pilotName);
            return true;
        }
        if (guard->isArmingAllowed()) {
            denialReason.clear();
            return true;
        }
        denialReason = guard->denialReason();
        if (denialReason.isEmpty()) {
            denialReason = QStringLiteral("Arming denied by preflight gate");
        }
        return false;
    };
}

// React to a check failure signal from PreflightManager.
// In Passive mode, failures are logged but do not block arming.
void ArmingGate::onCheckFailed(const QString &checkId, const QString &reason)
{
    Q_UNUSED(checkId)
    if (m_mode == Passive)
        return;

    if (m_manager && !m_manager->allMandatoryPassed()) {
        m_armingAllowed = false;
        m_denialReason = reason;
        emit armingAllowedChanged(false);
        emit denialReasonChanged(reason);
    }
}

// Re-evaluate arming state when all checks pass.
void ArmingGate::onAllChecksPassed()
{
    updateArmingState();
}

// Core evaluation: determines whether arming is allowed based on current state.
// Priority: override active > passive mode > telemetry staleness > check results.
// Persists the arming status to the database for dashboard display.
void ArmingGate::updateArmingState()
{
    if (m_overrideActive) {
        if (!m_armingAllowed) {
            m_armingAllowed = true;
            emit armingAllowedChanged(true);
        }
        return;
    }

    if (m_mode == Passive) {
        if (!m_armingAllowed) {
            m_armingAllowed = true;
            emit armingAllowedChanged(true);
        }
        return;
    }

    // An absent timestamp means "no telemetry has ever been seen", which is
    // treated as stale rather than fresh. Requiring isValid() to declare the
    // link stale made "nothing has ever arrived" the single state that always
    // looked healthy, so a freshly built gate reported itself armable on the
    // strength of checks that had run against no vehicle at all.
    const bool noTickEver = !m_lastTickTime.isValid();
    const bool telemetryStale = (noTickEver ||
        m_lastTickTime.secsTo(QDateTime::currentDateTime()) > kTelemetryStalenessSec) &&
        !m_overrideActive;
    if (telemetryStale) {
        QString msg = noTickEver
            ? QStringLiteral("No telemetry received from the vehicle yet")
            : QStringLiteral("Telemetry stale — last update %1s ago")
                .arg(m_lastTickTime.secsTo(QDateTime::currentDateTime()));
        if (m_armingAllowed || m_denialReason != msg) {
            m_armingAllowed = false;
            m_denialReason = msg;
            emit armingAllowedChanged(false);
            emit denialReasonChanged(msg);
        }
        return;
    }

    bool allOk = m_manager && m_manager->allMandatoryPassed();
    QString failure = allOk ? QString() : (m_manager ? m_manager->armingBlocker() : QStringLiteral("No manager"));

    if (allOk != m_armingAllowed || failure != m_denialReason) {
        m_armingAllowed = allOk;
        m_denialReason = failure;
        emit armingAllowedChanged(allOk);
        emit denialReasonChanged(failure);

        // Persist preflight status to DB
        if (m_telemetry && m_telemetry->vehicle()) {
            QString deviceUid = QString::number(m_telemetry->vehicle()->id());
            QString status = allOk ? QStringLiteral("Ready") : QStringLiteral("Blocked");
            DatabaseManager::instance().updatePreflightStatus(deviceUid, status);
        }
    }
}

bool ArmingGate::evaluateGate()
{
    if (m_overrideActive || m_mode == Passive)
        return true;
    return m_manager && m_manager->allMandatoryPassed();
}

QString ArmingGate::buildDenialReason(int criticalFailCount, int manualFailCount) const
{
    QStringList parts;
    if (criticalFailCount > 0) {
        parts << QStringLiteral("%1 blocking check(s) failed").arg(criticalFailCount);
    }
    if (manualFailCount > 0) {
        parts << QStringLiteral("%1 manual check(s) pending/failed").arg(manualFailCount);
    }
    if (parts.isEmpty()) {
        return QStringLiteral("Preflight checks not complete");
    }
    return parts.join(QStringLiteral("; "));
}
