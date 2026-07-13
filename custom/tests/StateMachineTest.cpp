#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "PreflightStateMachine.h"

class StateMachineTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testInitialState();
    void testValidTransitions();
    void testReset();
    void testStateChangedSignal();
};

void StateMachineTest::testInitialState()
{
    PreflightStateMachine sm;
    QCOMPARE(sm.state(), PreflightStateMachine::Disconnected);
    QCOMPARE(sm.stateName(), QStringLiteral("Disconnected"));
}

void StateMachineTest::testValidTransitions()
{
    PreflightStateMachine sm;

    sm.transitionTo(PreflightStateMachine::Connecting);
    QCOMPARE(sm.state(), PreflightStateMachine::Connecting);

    sm.transitionTo(PreflightStateMachine::ParamLoading);
    QCOMPARE(sm.state(), PreflightStateMachine::ParamLoading);

    sm.transitionTo(PreflightStateMachine::ChecklistInProgress);
    QCOMPARE(sm.state(), PreflightStateMachine::ChecklistInProgress);

    sm.transitionTo(PreflightStateMachine::PreflightPass);
    QCOMPARE(sm.state(), PreflightStateMachine::PreflightPass);

    sm.transitionTo(PreflightStateMachine::ArmingAllowed);
    QCOMPARE(sm.state(), PreflightStateMachine::ArmingAllowed);

    sm.transitionTo(PreflightStateMachine::Armed);
    QCOMPARE(sm.state(), PreflightStateMachine::Armed);
}

void StateMachineTest::testReset()
{
    PreflightStateMachine sm;
    sm.transitionTo(PreflightStateMachine::Armed);
    QCOMPARE(sm.state(), PreflightStateMachine::Armed);

    sm.reset();
    QCOMPARE(sm.state(), PreflightStateMachine::Disconnected);
}

void StateMachineTest::testStateChangedSignal()
{
    PreflightStateMachine sm;
    QSignalSpy spy(&sm, &PreflightStateMachine::stateChanged);

    sm.transitionTo(PreflightStateMachine::Connecting);
    QCOMPARE(spy.count(), 1);
}

UT_REGISTER_TEST(StateMachineTest)

#include "StateMachineTest.moc"
