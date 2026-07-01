#pragma once

#include <QObject>
#include <QTimer>

class PreflightStateMachine : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)

public:
    enum State {
        Disconnected = 0,
        Connecting,
        ParamLoading,
        ChecklistInProgress,
        PreflightPass,
        ManualConfirmPhase,
        ArmingAllowed,
        Armed
    };
    Q_ENUM(State)

    explicit PreflightStateMachine(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString stateName() const;

    Q_INVOKABLE void transitionTo(State s);
    Q_INVOKABLE void reset();

signals:
    void stateChanged();

    void linkEstablished();
    void paramLoadComplete();
    void firstUserConfirmation();
    void allAutoChecksPassed();
    void armedHeartbeatReceived();
    void disarmed();

private:
    void emitTransitionSignals(State oldState, State newState);

    State m_state = Disconnected;
};
