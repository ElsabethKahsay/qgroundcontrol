/**
 * @file PreflightStateMachine.h
 * @brief Finite state machine tracking the vehicle's preflight progression.
 *
 * States advance linearly: Disconnected → Connecting → ParamLoading →
 * ChecklistInProgress → PreflightPass → ManualConfirmPhase → ArmingAllowed → Armed.
 *
 * The PreflightManager drives transitions via transitionTo(); this class
 * emits semantic signals (e.g. linkEstablished, paramLoadComplete) that
 * other components can connect to for state-specific behavior.
 */

#pragma once

#include <QObject>
#include <QTimer>

/**
 * Tracks the lifecycle of preflight evaluation for a single vehicle.
 *
 * Transitions are driven externally by PreflightManager based on telemetry
 * connectivity, parameter readiness, and check evaluation results.
 */
class PreflightStateMachine : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)

public:
    /// Linear state progression from disconnected to armed.
    enum State {
        Disconnected = 0,       ///< No vehicle connected
        Connecting,             ///< Heartbeat received, link establishing
        ParamLoading,           ///< Vehicle parameters being fetched
        ChecklistInProgress,    ///< Auto checks being evaluated
        PreflightPass,          ///< All auto mandatory checks passed
        ManualConfirmPhase,     ///< Waiting for operator to confirm manual checks
        ArmingAllowed,          ///< All mandatory checks passed, safe to arm
        Armed                   ///< Vehicle is armed (motors active)
    };
    Q_ENUM(State)

    explicit PreflightStateMachine(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString stateName() const;

    Q_INVOKABLE void transitionTo(State s);
    Q_INVOKABLE void reset();

signals:
    void stateChanged();

    /// Emitted when transitioning from Disconnected to Connecting/ParamLoading.
    void linkEstablished();
    /// Emitted when transitioning from ParamLoading to ChecklistInProgress.
    void paramLoadComplete();
    /// Emitted when entering ManualConfirmPhase.
    void firstUserConfirmation();
    /// Emitted when all auto mandatory checks have passed.
    void allAutoChecksPassed();
    /// Emitted when vehicle transitions to Armed state.
    void armedHeartbeatReceived();
    /// Emitted when reset() moves back to Disconnected.
    void disarmed();

private:
    void emitTransitionSignals(State oldState, State newState);

    State m_state = Disconnected;
};
