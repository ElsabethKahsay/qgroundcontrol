/**
 * @file PreflightStateMachine.cpp
 * @brief Finite state machine implementation for the preflight evaluation lifecycle.
 *
 * Manages linear state transitions (Disconnected through Armed) and emits
 * semantic signals on key milestones (link established, param load complete,
 * etc.) so other components can react to specific lifecycle events.
 */

#include "PreflightStateMachine.h"

PreflightStateMachine::PreflightStateMachine(QObject *parent)
    : QObject(parent)
{
}

// Return a human-readable name for the current state, for display in QML.
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

// Transition to a new state. No-ops if already in the target state.
// Emits stateChanged() plus any transition-specific semantic signals.
void PreflightStateMachine::transitionTo(State s)
{
    if (m_state == s)
        return;

    State oldState = m_state;
    m_state = s;

    emit stateChanged();
    emitTransitionSignals(oldState, s);
}

// Reset to Disconnected. Emits disarmed() if we were in any other state.
void PreflightStateMachine::reset()
{
    State oldState = m_state;
    m_state = Disconnected;
    emit stateChanged();
    if (oldState != Disconnected)
        emit disarmed();
}

// Emit transition-specific signals based on the state being entered.
// This allows listeners to react to specific lifecycle milestones
// (e.g. start logging when linkEstablished fires).
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
