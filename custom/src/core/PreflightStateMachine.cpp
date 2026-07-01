#include "PreflightStateMachine.h"

PreflightStateMachine::PreflightStateMachine(QObject *parent)
    : QObject(parent)
{
}

QString PreflightStateMachine::stateName() const
{
    switch (m_state) {
    case Disconnected:      return QStringLiteral("Disconnected");
    case Connecting:        return QStringLiteral("Connecting");
    case ParamLoading:      return QStringLiteral("Loading Parameters");
    case ChecklistInProgress: return QStringLiteral("Checklist In Progress");
    case PreflightPass:     return QStringLiteral("Preflight Passed");
    case ManualConfirmPhase: return QStringLiteral("Manual Confirmation Required");
    case ArmingAllowed:     return QStringLiteral("Ready to Arm");
    case Armed:             return QStringLiteral("Armed");
    }
    return QStringLiteral("Unknown");
}

void PreflightStateMachine::transitionTo(State s)
{
    if (m_state == s)
        return;

    State oldState = m_state;
    m_state = s;

    emit stateChanged();
    emitTransitionSignals(oldState, s);
}

void PreflightStateMachine::reset()
{
    State oldState = m_state;
    m_state = Disconnected;
    emit stateChanged();
    if (oldState != Disconnected)
        emit disarmed();
}

void PreflightStateMachine::emitTransitionSignals(State oldState, State newState)
{
    switch (newState) {
    case Connecting:
        if (oldState == Disconnected)
            emit linkEstablished();
        break;
    case ParamLoading:
        if (oldState == Connecting)
            emit linkEstablished();
        break;
    case ChecklistInProgress:
        if (oldState == ParamLoading)
            emit paramLoadComplete();
        break;
    case PreflightPass:
        emit allAutoChecksPassed();
        break;
    case ManualConfirmPhase:
        emit firstUserConfirmation();
        break;
    case ArmingAllowed:
        break;
    case Armed:
        emit armedHeartbeatReceived();
        break;
    default:
        break;
    }
}
