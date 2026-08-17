#pragma once
#include <QObject>
#include <QDateTime>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class FlightSession : public QObject
{
    Q_OBJECT
    QML_SINGLETON

    Q_PROPERTY(SessionMode  mode          READ mode          NOTIFY modeChanged)
    Q_PROPERTY(SessionState state         READ state         NOTIFY stateChanged)
    Q_PROPERTY(int          currentFlightId READ currentFlightId NOTIFY flightIdChanged)
    Q_PROPERTY(bool         armingPermitted READ armingPermitted NOTIFY armingPermittedChanged)
    Q_PROPERTY(bool         isTraining    READ isTraining    NOTIFY modeChanged)
    Q_PROPERTY(bool         isTesting     READ isTesting     NOTIFY modeChanged)
    Q_PROPERTY(bool         isFlight      READ isFlight      NOTIFY modeChanged)
    Q_PROPERTY(bool         formComplete  READ formComplete  NOTIFY formCompleteChanged)
    Q_PROPERTY(QString      sessionSummary READ sessionSummary NOTIFY stateChanged)
    Q_PROPERTY(QString      instructorName READ instructorName NOTIFY instructorChanged)

public:
    enum class SessionMode {
        None,
        Training,
        Testing,
        Flight
    };
    Q_ENUM(SessionMode)

    enum class SessionState {
        Idle,
        AwaitingModeSelect,
        FormPending,
        PreFlight,
        ReadyToArm,
        Armed,
        PostFlight,
        Closed
    };
    Q_ENUM(SessionState)

    static FlightSession *instance();
    explicit FlightSession(QObject *parent = nullptr);

    SessionMode  mode()            const { return m_mode; }
    SessionState state()           const { return m_state; }
    int          currentFlightId() const { return m_flightId; }
    bool         armingPermitted() const;
    bool         isTraining()   const { return m_mode == SessionMode::Training; }
    bool         isTesting()    const { return m_mode == SessionMode::Testing; }
    bool         isFlight()     const { return m_mode == SessionMode::Flight; }
    bool         formComplete() const { return m_formComplete; }
    QString      sessionSummary() const;

    Q_INVOKABLE void onVehicleConnected();
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void requestForm();
    Q_INVOKABLE void cancelForm();
    Q_INVOKABLE void startTrainingSession();
    Q_INVOKABLE void startTestingSession();
    Q_INVOKABLE void setInstructor(int operatorId);
    Q_INVOKABLE void startFlightSession(const QString &purpose,
                                        const QString &location,
                                        const QString &notes);
    Q_INVOKABLE void onPreFlightComplete();
    Q_INVOKABLE void closeSession();

    void onVehicleArmed();
    void onVehicleDisarmed();
    void onDisarmedWithStats(double maxAltitude, double minBatteryV, double maxBatteryV, int modeChanges);
    void onFlightModeChanged(const QString &mode);
    void onCheckDegraded(const QString &checkId, const QString &newStatus);
    void onBatteryWarning(double voltageV, bool isCritical);
    void recordCheckResult(const QString &checkId, const QString &category,
                           bool isPostFlight, const QString &status,
                           const QString &message, int confirmedBy = -1);

    QString instructorName() const { return m_instructorName; }

signals:
    void modeChanged();
    void stateChanged();
    void flightIdChanged();
    void armingPermittedChanged();
    void formCompleteChanged();
    void sessionStarted(SessionMode mode);
    void sessionClosed(int flightId);
    void instructorChanged();
    void postFlightChecklistRequired();

private:
    void _setState(SessionState s);
    void _setMode(SessionMode m);
    QDateTime _sessionStartTime() const;

    SessionMode  m_mode         = SessionMode::None;
    SessionState m_state        = SessionState::Idle;
    int          m_flightId     = -1;
    bool         m_formComplete = false;
    QDateTime    m_armedAt;
    QDateTime    m_startedAt;
    double       m_maxAltitude  = 0.0;
    double       m_minBatteryV  = 999.0;
    double       m_maxBatteryV  = 0.0;
    int          m_modeChanges  = 0;
    int          m_instructorId = -1;
    QString      m_instructorName;
};
