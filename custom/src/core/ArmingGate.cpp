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
#include "utils/MonotonicClock.h"

#include <QDebug>
#include <QRandomGenerator>
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
        emit overrideKindChanged();
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

    // Acknowledgment expiry: an unused ALLOW_WITH_ACK is single-use AND
    // time-bounded. The token minted at acknowledgment time is cleared here, so
    // an ack made earlier can never authorize a later arm.
    m_ackTimer = new QTimer(this);
    m_ackTimer->setSingleShot(true);
    connect(m_ackTimer, &QTimer::timeout, this, [this]() {
        if (!m_ackReceived && m_ackToken == 0) {
            return;
        }
        m_ackReceived = false;
        m_ackToken = 0;
        m_ackCheckIds.clear();
        emit overrideKindChanged();
        qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK expired before use");
        updateArmingState();
    });

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
// arrival. Every autopilot HEARTBEAT stamps m_lastTickMs (monotonic), so:
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
            m_lastTickMs = MonotonicClock::nowMs();
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

// INBOUND arm/disarm COMMAND_LONG from a link peer (companion, another GCS).
// Deny, never mutate: the peer's arm is judged
// by decide() with token 0, so it can never spend the operator's
// acknowledgment or a force token, and this path never changes gate state.
// It is denied when the gate is closed (matters when QGC forwards between
// links) and every peer arm is logged. Disarm always passes.
bool ArmingGate::interceptCommandLong(uint16_t command, const QMap<int, float> &params)
{
    const bool isArm = qFuzzyCompare(params.value(1, 0.0f), 1.0f);
    if (!isArm)
        return true;

    const ArmDecision d = decide(currentRequest(0, true), snapshot());
    if (d.verdict == ArmDecision::Allow) {
        qWarning().noquote() << QStringLiteral("ArmingGate: inbound ARM from a link peer allowed (gate open)");
        return true;
    }
    qWarning().noquote() << QStringLiteral("ArmingGate: inbound ARM from a link peer denied: %1").arg(d.reason);
    emit armingDenied(command, d.reason);
    return false;
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
    m_lastAck.timestamp = QDateTime::currentDateTimeUtc();   // stored: UTC

    // Mint a fresh non-zero token bound to this acknowledgment. Only the arm
    // command presenting this token (TelemetryBridge::arm() attaches it to the
    // vehicle) may consume the ack; a stock arm (token 0) never can.
    m_ackToken = QRandomGenerator::global()->generate();
    if (m_ackToken == 0) {
        m_ackToken = 1;
    }
    // Single-shot expiry: an unused ack does not survive the operator's window.
    m_ackTimer->setInterval(m_ackExpiryMs);
    m_ackTimer->start();
    // The ack covers exactly the checks failing now; a new failure voids it.
    m_ackCheckIds = currentRequest(0).failingMandatoryIds;
    emit overrideKindChanged();

    qWarning().noquote() << QStringLiteral("ArmingGate: ALLOW_WITH_ACK acknowledged by %1 — %2")
                                .arg(pilotName, reason);
    emit overrideAcknowledged(pilotName, reason);
    return true;
}

void ArmingGate::setOverridePolicy(OverridePolicy policy)
{
    m_overridePolicy = policy;
}

// Reset the gate to normal evaluation mode, clearing any active override.
void ArmingGate::resetGate()
{
    m_overrideActive = false;
    m_ackReceived = false;
    m_ackToken = 0;
    if (m_ackTimer) m_ackTimer->stop();
    m_overrideTimer->stop();
    m_overrideToken = 0;
    // m_overrideTokenSeq is never reset, so a token value is never reused.
    m_forceArmOverride = false;
    m_ackCheckIds.clear();
    emit overrideActiveChanged(false);
    emit overrideKindChanged();
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

    // Mint a one-shot caller-scoped token for the sanctioned caller.
    m_forceArmOverride = true;
    const quint32 token = ++m_overrideTokenSeq;
    m_overrideToken = token;

    // Close the window automatically so the gate does not stay open forever.
    m_overrideTimer->start(timeoutMs);
    emit overrideKindChanged();
    updateArmingState();   // a plain arm is still denied during the window
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

    // Mint a one-shot caller-scoped token for the pending command about to be
    // issued by the sanctioned caller.
    m_forceArmOverride = true;
    const quint32 token = ++m_overrideTokenSeq;
    m_overrideToken = token;

    emit gateOverrideLogged(QStringLiteral("Operator force arm"), kForceArmOverrideSec);
    qWarning().noquote() << QStringLiteral("Arming overridden: Operator force arm (%1s window)").arg(kForceArmOverrideSec);

    // Close the window automatically so the gate does not stay open forever;
    // it also closes as soon as the token is used (consumeForceToken).
    m_overrideTimer->start(kForceArmOverrideSec * 1000);
    emit overrideKindChanged();
    updateArmingState();   // a plain arm is still denied during the window
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
std::function<bool(QString&, quint32)> ArmingGate::makeArmInterceptor()
{
    QPointer<ArmingGate> guard(this);
    return [guard](QString &denialReason, quint32 callToken) -> bool {
        if (!guard) {
            denialReason = QStringLiteral("Arming gate unavailable (fail-closed)");
            return false;
        }
        const ArmDecision d = decide(guard->currentRequest(callToken), guard->snapshot());
        if (d.verdict == ArmDecision::Allow) {
            if (d.kind == ArmDecision::Force)
                guard->consumeForceToken();
            else if (d.kind == ArmDecision::Ack)
                guard->consumeAck();
            denialReason.clear();
            return true;
        }
        denialReason = d.reason.isEmpty() ? QStringLiteral("Arming denied by preflight gate") : d.reason;
        return false;
    };
}

// A check failure re-runs the single evaluation path instead of writing
// gate state directly.
void ArmingGate::onCheckFailed(const QString &checkId, const QString &reason)
{
    Q_UNUSED(checkId)
    Q_UNUSED(reason)
    updateArmingState();
}

// Re-evaluate arming state when all checks pass.
void ArmingGate::onAllChecksPassed()
{
    updateArmingState();
}

// armingAllowed answers "would a plain arm (token 0) pass now".
// Persists the arming status to the database for dashboard display.
void ArmingGate::updateArmingState()
{
    const ArmDecision d = decide(currentRequest(0), snapshot());
    const bool allowed = (d.verdict == ArmDecision::Allow);
    const QString reason = allowed ? QString() : d.reason;
    if (allowed == m_armingAllowed && reason == m_denialReason)
        return;
    const bool allowedChanged = (allowed != m_armingAllowed);
    m_armingAllowed = allowed;
    m_denialReason = reason;
    if (allowedChanged)
        emit armingAllowedChanged(allowed);
    emit denialReasonChanged(reason);

    if (allowedChanged && m_telemetry && m_telemetry->vehicle()) {
        const QString deviceUid = QString::number(m_telemetry->vehicle()->id());
        DatabaseManager::instance().updatePreflightStatus(deviceUid, allowed ? QStringLiteral("Ready")
                                                                             : QStringLiteral("Blocked"));
    }
}

ArmingGate::ArmDecision ArmingGate::decide(const ArmRequest &request, const GateSnapshot &gate)
{
    ArmDecision d;
    d.failingIds = request.failingMandatoryIds;
    auto deny = [&d](const QString &reason) {
        d.verdict = ArmDecision::Deny;
        d.reason = reason;
        return d;
    };
    auto allow = [&d](ArmDecision::Kind kind) {
        d.verdict = ArmDecision::Allow;
        d.kind = kind;
        d.reason.clear();
        return d;
    };

    // 1. Disarm is never blocked.
    if (!request.isArm)
        return allow(ArmDecision::Normal);

    // 2. Training: no GCS arm at all, including the GCS's own buttons.
    if (request.training)
        return deny(QStringLiteral("Arming is disabled in training mode"));

    // 3. Force window: exactly the minted token, once; nothing else passes.
    if (gate.forceWindowOpen) {
        if (request.callToken != 0 && request.callToken == gate.forceToken)
            return allow(ArmDecision::Force);
        return deny(QStringLiteral("Force-arm override is one-shot and caller-scoped; "
                                   "this arm command does not hold the minted token"));
    }

    // 4. Passive mode: test-only (QML cannot set the mode).
    if (gate.mode == Passive)
        return allow(ArmDecision::Normal);

    // 5. Hard blocks — never overridable.
    if (request.heartbeatAgeMs < 0)
        return deny(QStringLiteral("No telemetry received from the vehicle yet"));
    if (request.heartbeatAgeMs > qint64(kTelemetryStalenessSec) * 1000)
        return deny(QStringLiteral("Telemetry stale — last update %1s ago").arg(request.heartbeatAgeMs / 1000));
    if (request.anyMandatoryStale)
        return deny(request.blocker.isEmpty() ? QStringLiteral("Telemetry data stale") : request.blocker);
    if (!request.batteryPackSelected)
        return deny(QStringLiteral("Select the battery pack before arming"));

    // 6. Everything required passes.
    if (request.allMandatoryPassed)
        return allow(ArmDecision::Normal);

    const QString blocker = request.blocker.isEmpty() ? QStringLiteral("Preflight checks not complete")
                                                      : request.blocker;
    // 7. Overridable failures: the operator's bound, expiring acknowledgment.
    //    (Any failing check except the hard blocks.)
    if (gate.policy == ALLOW_WITH_ACK) {
        if (gate.ackValid && request.callToken != 0 && request.callToken == gate.ackToken) {
            QStringList newFailures;
            for (const QString &id : request.failingMandatoryIds) {
                if (!gate.ackCheckIds.contains(id))
                    newFailures << id;
            }
            if (newFailures.isEmpty())
                return allow(ArmDecision::Ack);
            return deny(QStringLiteral("New failure since the acknowledgment (%1) — acknowledge again")
                            .arg(newFailures.join(QStringLiteral(", "))));
        }
        d.verdict = ArmDecision::NeedsAck;
        d.reason = blocker;
        return d;
    }

    // 8. Everything else.
    return deny(blocker);
}

ArmingGate::ArmRequest ArmingGate::currentRequest(quint32 callToken, bool isArm) const
{
    ArmRequest r;
    r.isArm = isArm;
    r.callToken = callToken;
    r.heartbeatAgeMs = MonotonicClock::ageMs(m_lastTickMs);
    if (m_manager) {
        r.allMandatoryPassed = m_manager->allMandatoryPassed();
        r.blocker = m_manager->armingBlocker();
        for (const AbstractCheck *c : m_manager->checks()) {
            if (!c || !c->mandatory() || c->isInformational())
                continue;
            const CheckStatus st = c->status();
            if (st == CheckStatus::Passed || st == CheckStatus::Warning || st == CheckStatus::Skipped)
                continue;
            r.failingMandatoryIds << c->id();
            // No pack selected is a hard block, in every mode.
            if (c->id() == QLatin1String("power.battery.pack"))
                r.batteryPackSelected = false;
            if (st == CheckStatus::Stale)
                r.anyMandatoryStale = true;
        }
    } else {
        r.blocker = QStringLiteral("No preflight manager");
    }
    FlightSession *session = FlightSession::instance();
    r.training = session && session->isTraining();
    return r;
}

ArmingGate::GateSnapshot ArmingGate::snapshot() const
{
    GateSnapshot g;
    g.mode = m_mode;
    g.policy = m_overridePolicy;
    g.forceWindowOpen = m_overrideActive && m_forceArmOverride;
    g.forceToken = m_overrideToken;
    g.ackValid = m_ackReceived && m_ackToken != 0;
    g.ackToken = m_ackToken;
    g.ackCheckIds = m_ackCheckIds;
    return g;
}

ArmingGate::OverrideKind ArmingGate::overrideKind() const
{
    if (m_overrideActive && m_forceArmOverride)
        return ForceOverride;
    if (m_ackReceived && m_ackToken != 0)
        return AckOverride;
    return NoOverride;
}

QVariantMap ArmingGate::requestArm()
{
    // The bridge's arm() attaches the acknowledgment token itself; decide with
    // it here so the reply matches what the interceptor will decide.
    const quint32 token = (m_ackReceived ? m_ackToken : 0);
    ArmDecision d = decide(currentRequest(token), snapshot());
    if (d.verdict == ArmDecision::Allow) {
        if (m_telemetry && m_telemetry->vehicle()) {
            m_telemetry->arm();
        } else {
            d.verdict = ArmDecision::Deny;
            d.reason = QStringLiteral("No vehicle connected");
        }
    }
    if (d.verdict == ArmDecision::Deny)
        emit commandRejected(d.reason);
    return toVariantMap(d);
}

QVariantMap ArmingGate::previewArm() const
{
    const quint32 token = (m_ackReceived ? m_ackToken : 0);
    return toVariantMap(decide(currentRequest(token), snapshot()));
}

void ArmingGate::consumeAck()
{
    qWarning().noquote() << QStringLiteral("ArmingGate: acknowledged override used by pilot %1 (reason: %2; checks: %3)")
                                .arg(m_lastAck.pilotName, m_lastAck.reason,
                                     m_ackCheckIds.join(QStringLiteral(", ")));
    m_ackReceived = false;
    m_ackToken = 0;
    m_ackCheckIds.clear();
    if (m_ackTimer)
        m_ackTimer->stop();
    emit overrideKindChanged();
    updateArmingState();
}

void ArmingGate::consumeForceToken()
{
    m_overrideToken = 0;
    m_overrideActive = false;
    m_forceArmOverride = false;
    if (m_overrideTimer)
        m_overrideTimer->stop();
    emit overrideActiveChanged(false);
    emit overrideKindChanged();
    updateArmingState();
}

QVariantMap ArmingGate::toVariantMap(const ArmDecision &d)
{
    static const char *verdicts[] = {"allow", "needsAck", "deny"};
    static const char *kinds[] = {"normal", "ack", "force"};
    QVariantMap m;
    m.insert(QStringLiteral("verdict"), QString::fromLatin1(verdicts[d.verdict]));
    m.insert(QStringLiteral("kind"), QString::fromLatin1(kinds[d.kind]));
    m.insert(QStringLiteral("reason"), d.reason);
    m.insert(QStringLiteral("failing"), d.failingIds);
    return m;
}
