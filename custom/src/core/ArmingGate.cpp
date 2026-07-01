#include "ArmingGate.h"

#include <QDebug>
#include <QTimer>

#include "AbstractCheck.h"
#include "PreflightManager.h"
#include "TelemetryBridge.h"

QSet<uint16_t> ArmingGate::s_emergencyCommands = {
    21,    // MAV_CMD_DO_SET_MODE
    92,    // MAV_CMD_DO_DISARM
    2050,  // MAV_CMD_COMPONENT_ARM_DISARM (disarm)
    3000,  // MAV_CMD_DO_LAND_START
    4000   // MAV_CMD_NAV_LAND
};

ArmingGate::ArmingGate(QObject *parent)
    : QObject(parent)
{
    m_overrideTimer = new QTimer(this);
    m_overrideTimer->setSingleShot(true);
    connect(m_overrideTimer, &QTimer::timeout, this, [this]() {
        m_overrideActive = false;
        emit overrideActiveChanged(false);
        emit armingOverrideExpired();
    });

    m_gateTimer = new QTimer(this);
    m_gateTimer->setInterval(EVAL_INTERVAL_MS);
    connect(m_gateTimer, &QTimer::timeout, this, &ArmingGate::updateArmingState);
}

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
    updateArmingState();
}

void ArmingGate::setTelemetryBridge(TelemetryBridge *bridge)
{
    m_telemetry = bridge;
}

void ArmingGate::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    emit modeChanged(mode);
    updateArmingState();
}

bool ArmingGate::interceptCommandLong(uint16_t command, const QMap<int, float> &params)
{
    if (command != m_armCommandCode)
        return true;

    float armParam = params.value(1, 0.0f);
    bool isArm = qFuzzyCompare(armParam, 1.0f);
    if (!isArm)
        return true;

    if (m_overrideActive)
        return true;

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

    if (s_emergencyCommands.contains(command))
        return true;

    if (m_mode == Passive)
        return true;

    if (!m_manager) {
        qWarning() << "ArmingGate: no PreflightManager set";
        return true;
    }

    bool allPassed = m_manager->allMandatoryPassed();

    if (m_mode == Hybrid && !allPassed) {
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

    if (m_mode == Active && !allPassed) {
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

    // ACTIVE + any blocking fail = suppress
    if (m_mode == Active && criticalFailCount > 0) {
        QString reason = buildDenialReason(criticalFailCount, manualFailCount);
        m_denialReason = reason;
        m_armingAllowed = false;
        emit armingAllowedChanged(false);
        emit denialReasonChanged(reason);
        emit gateClosed(reason);
        return GATE_CLOSED;
    }

    // HYBRID + any auto blocking fail = suppress (manual-only failures may be overridden)
    if (m_mode == Hybrid && criticalFailCount > 0) {
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

void ArmingGate::resetGate()
{
    m_overrideActive = false;
    m_ackReceived = false;
    m_overrideTimer->stop();
    emit overrideActiveChanged(false);
    updateArmingState();
}

void ArmingGate::forceArm()
{
    overrideGate(QStringLiteral("Operator force arm"), 10);
}

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

void ArmingGate::onAllChecksPassed()
{
    updateArmingState();
}

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
        m_lastTickTime.secsTo(QDateTime::currentDateTime()) > 10 &&
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
