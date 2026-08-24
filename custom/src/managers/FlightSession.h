#pragma once
#include <QObject>
#include <QDateTime>
#include <QVariantMap>
#include <limits>
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
    /// Operator-confirmed target location for the current flight (persisted
    /// on flight_sessions via saveTargetLocation).  NaN/"" when not yet set.
    Q_PROPERTY(double       targetLat    READ targetLat    NOTIFY targetLocationChanged)
    Q_PROPERTY(double       targetLon    READ targetLon    NOTIFY targetLocationChanged)
    Q_PROPERTY(QString      targetSource READ targetSource NOTIFY targetLocationChanged)
    /// Last saveTargetLocation() failure reason ("" when the last call succeeded).
    Q_PROPERTY(QString      lastTargetError READ lastTargetError NOTIFY lastTargetErrorChanged)

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
    double       targetLat()    const { return m_targetLat; }
    double       targetLon()    const { return m_targetLon; }
    QString      targetSource() const { return m_targetSource; }
    QString      lastTargetError() const { return m_lastTargetError; }

    /// Validates and persists the target location for the current flight.
    /// source is a short tag such as "map center" or "manual".  Returns false
    /// (with lastTargetError set) when no flight is active or the coordinates
    /// are missing/out of range — nothing is written to the DB in that case.
    Q_INVOKABLE bool saveTargetLocation(double lat, double lon, const QString &source);
    /// Re-reads the persisted target location for the current flight id.
    Q_INVOKABLE void reloadTargetLocation();

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
    void onDisarmedWithStats(double maxAltitude, double minBatteryV, double maxBatteryV, int modeChanges,
                             double maxGroundSpeedMs = 0.0, double maxVerticalSpeedMs = 0.0,
                             double distanceFlownM = 0.0, double avgBatteryV = 0.0);
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
    void targetLocationChanged();
    void lastTargetErrorChanged();

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
    double       m_maxGroundSpeedMs = 0.0;
    double       m_maxVerticalSpeedMs = 0.0;
    double       m_distanceFlownM = 0.0;
    double       m_avgBatteryV  = 0.0;
    int          m_instructorId = -1;
    QString      m_instructorName;
    double       m_targetLat    = std::numeric_limits<double>::quiet_NaN();
    double       m_targetLon    = std::numeric_limits<double>::quiet_NaN();
    QString      m_targetSource;
    QString      m_lastTargetError;
};
