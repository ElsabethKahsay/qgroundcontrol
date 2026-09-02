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
        emit overrideActiveChanged(false);
        emit armingOverrideExpired();
    });

    // Gate timer: periodically re-evaluates arming state at a fixed interval
    m_gateTimer = new QTimer(this);
    m_gateTimer->setInterval(EVAL_INTERVAL_MS);
    connect(m_gateTimer, &QTimer::timeout, this, &ArmingGate::updateArmingState);
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
        connect(m_manager, &PreflightManager::progressChanged,
                this, [this]() {
                    m_lastTickTime = QDateTime::currentDateTime();
                });
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

// Store the telemetry bridge reference for vehicle state queries during gate evaluation.
void ArmingGate::setTelemetryBridge(TelemetryBridge *bridge)
{
    m_telemetry = bridge;
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
    emit overrideActiveChanged(false);
    updateArmingState();
}

// Unconditionally allow arming with no timeout. Used for emergency overrides.
// Logs the action for audit trail with no expiration.
void ArmingGate::forceArm()
{
    m_overrideActive = true;
    emit overrideActiveChanged(true);
    emit armingOverrideActivated(QStringLiteral("Operator force arm"), 0);

    m_denialReason.clear();
    m_armingAllowed = true;
    emit armingAllowedChanged(true);
    emit denialReasonChanged({});

    emit gateOverrideLogged(QStringLiteral("Operator force arm"), 0);
    qWarning() << "Arming overridden: Operator force arm (no timeout)";
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

    bool telemetryStale = m_lastTickTime.isValid() &&
        m_lastTickTime.secsTo(QDateTime::currentDateTime()) > kTelemetryStalenessSec &&
        !m_overrideActive;
    if (telemetryStale) {
        QString msg = QStringLiteral("Telemetry stale — last update %1s ago")
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
