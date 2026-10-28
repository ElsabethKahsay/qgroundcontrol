// The ONE writer of connection-session and
// flight rows. Everything else tells it what happened:
//
//   connection  opens when the vehicle's UID is known, closes on vehicle
//               removal or app exit (connection_sessions)
//   flight      opens on the vehicle's armed transition, whoever armed, and
//               closes on disarm (flight_sessions). Arm source, gate state,
//               session mode and the bench flag are stored, not used as
//               triggers.
//   link loss   while armed: the flight stays open with a link_lost event; if
//               the link returns armed it continues, if it returns disarmed or
//               the vehicle goes away it closes with end_reason = link_lost at
//               the moment the link was lost.
//   summary     every 10 s the open flight's duration, minimum voltage and mAh
//               used are written, and the connection's last-seen time; crash
//               recovery (DatabaseManager::recoverOrphanedSessions) closes any
//               row left open at that time with end_reason = crash_recovered.
//   power type  once a second, a non-bench flight of an aircraft linked to a
//               power type is split into hover/cruise and loaded/empty time
//               with the FC current and throttle per phase
//               (PowerTypeModel::FlightTracker); the result is written with
//               each summary and at close, for the model to learn from.
//
// Durations come from MonotonicClock; timestamps are UTC ISO-8601.
// Header-only and without Q_OBJECT (functor connections only), so it needs
// no CMake or moc change, and tests drive it without a vehicle or MockLink.
#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <QDateTime>
#include <QVariantMap>
#include <QObject>
#include <QTimer>

#include "PowerTypeModel.h"
#include "utils/DatabaseManager.h"
#include "utils/MonotonicClock.h"

class SessionRecorder : public QObject {
public:
    static constexpr int kSummaryIntervalMs = 10000;
    static constexpr int kPowerTickMs = 1000;

    /// Live values the running summary reads.
    struct Sample {
        double voltage = std::numeric_limits<double>::quiet_NaN();
        double mahConsumed = -1;   ///< FC counter for the pack; -1 = unknown
        int percent = -1;          ///< state of charge; -1 = unknown
        double energyWh = -1;      ///< FC energy counter (BATTERY_STATUS.energy_consumed); -1 = unknown
        double distanceM = -1;     ///< vehicle's flown-distance counter; -1 = unknown
        double altitudeM = std::numeric_limits<double>::quiet_NaN();   ///< relative altitude
        int gpsSats = -1;
        QString flightMode;
        double groundSpeedMs = std::numeric_limits<double>::quiet_NaN();
        double currentA = -1;      ///< FC current (synthetic or measured); -1 = unknown
        double throttle = -1;      ///< 0–1; -1 = unknown
        double lat = std::numeric_limits<double>::quiet_NaN();
        double lon = std::numeric_limits<double>::quiet_NaN();
    };

    /// What is known at the arm transition.
    struct FlightStart {
        QString batterySerial;
        double payloadKg = 0.0;
        QString armSource;         ///< "gcs", "force_token" or "external" (RC / another GCS)
        bool gateAllowed = false;  ///< the arming gate's verdict at that moment
        QString mode;              ///< session mode: Flight, Testing, Training
        bool bench = false;        ///< motor/servo test or Testing mode: no hours, no cycles
        QVariantMap pack;          ///< DatabaseManager::batteryPack() of the selected pack, frozen at arm
        int operatorId = -1;       ///< for the Flight History row; -1 = none selected
        int vehicleSysId = 0;
        QString weatherSummary;
        int powerTypeId = -1;      ///< aircraft's power type; -1 = not linked (no power record)
        int cells = 0;             ///< the power type's cell count, for the full-pack test
        double timeAtTargetMin = -1;
        double targetLat = std::numeric_limits<double>::quiet_NaN();
        double targetLon = std::numeric_limits<double>::quiet_NaN();
    };

    /// What a closed flight was, for the caller's follow-up (battery cycle...).
    struct FlightEnd {
        int flightId = -1;
        QString deviceUid;
        QString batterySerial;
        bool bench = false;
        QString endReason;
        double durationSec = 0.0;
        double mahUsed = -1;       ///< -1 = unknown
        double whUsed = -1;        ///< -1 = unknown
    };

    /// Energy used. The FC's energy counter when it reports one;
    /// otherwise mAh used x mean voltage; -1 when neither is known.
    static double energyUsedWh(double startWh, double endWh, double mahUsed, double startV, double endV)
    {
        if (startWh >= 0 && endWh >= startWh)
            return endWh - startWh;
        if (mahUsed >= 0 && std::isfinite(startV) && std::isfinite(endV) && startV > 0 && endV > 0)
            return mahUsed * (startV + endV) / 2.0 / 1000.0;
        return -1;
    }

    explicit SessionRecorder(DatabaseManager &db, QObject *parent = nullptr)
        : QObject(parent), m_db(db)
    {
        m_summaryTimer.setInterval(kSummaryIntervalMs);
        QObject::connect(&m_summaryTimer, &QTimer::timeout, this, [this]() { writeSummary(); });
        m_powerTimer.setInterval(kPowerTickMs);
        QObject::connect(&m_powerTimer, &QTimer::timeout, this, [this]() {
            const qint64 now = MonotonicClock::nowMs();
            powerTick((now - m_lastPowerTickMs) / 1000.0);
            m_lastPowerTickMs = now;
        });
    }

    ~SessionRecorder() override { connectionClosed(QStringLiteral("app_exit")); }

    void setSampleProvider(std::function<Sample()> provider) { m_sample = std::move(provider); }
    void setFlightClosedHandler(std::function<void(const FlightEnd &)> handler) { m_onFlightClosed = std::move(handler); }

    int connectionId() const { return m_connId; }
    int flightId() const { return m_flightId; }
    bool flightOpen() const { return m_flightId > 0; }
    QString deviceUid() const { return m_deviceUid; }

    /// The vehicle's UID is known. A different UID closes the previous
    /// connection first; the same UID again is a no-op.
    void connectionOpened(const QString &deviceUid, const QString &mode,
                          const QString &operatorName, const QString &appVersion)
    {
        if (deviceUid.isEmpty() || (m_connId > 0 && deviceUid == m_deviceUid))
            return;
        if (m_connId > 0)
            connectionClosed(QStringLiteral("vehicle_changed"));
        m_deviceUid = deviceUid;
        m_connId = m_db.openConnectionSession(deviceUid, mode, operatorName, appVersion);
        m_linkLost = false;
        updateContext();
        m_summaryTimer.start();
    }

    /// arm_source stored for a flight the GCS joined with the vehicle already armed.
    static inline const QString kArmSourceAlreadyArmed = QStringLiteral("already_armed");

    /// The vehicle was already armed when the GCS came to know it: the GCS
    /// was restarted mid-flight (after a crash, its old row is closed as
    /// crash_recovered at start-up), or the arm arrived before the vehicle's
    /// UID, when armed() had no row to key the flight to. Neither case gives
    /// another arm transition, so without this the rest of the flight is
    /// never recorded. Opens the flight now, counted from this moment (the
    /// start sample is taken now, so time and energy are the remainder), with
    /// arm_source = already_armed because who armed it is not known. Does
    /// nothing when a flight is already open or no connection is open.
    /// Returns true when a flight was opened.
    bool joinArmedFlight(FlightStart start)
    {
        if (m_flightId > 0 || m_connId <= 0 || m_deviceUid.isEmpty())
            return false;
        start.armSource = kArmSourceAlreadyArmed;
        armed(start);
        return m_flightId > 0;
    }

    /// Vehicle removed or app exit. An open flight closes first: link_lost if
    /// the link was already gone, else @p reason.
    void connectionClosed(const QString &reason)
    {
        if (m_flightId > 0)
            closeFlight(m_linkLost ? QStringLiteral("link_lost") : reason);
        if (m_connId > 0)
            m_db.closeConnectionSession(m_connId, m_linkLost ? QStringLiteral("link_lost") : reason);
        m_connId = -1;
        m_deviceUid.clear();
        m_linkLost = false;
        m_summaryTimer.stop();
        updateContext();
    }

    /// Armed transition (from the HEARTBEAT, via ArmStateTracker).
    void armed(const FlightStart &start)
    {
        if (m_flightId > 0)
            closeFlight(QStringLiteral("rearmed_without_disarm"));
        if (m_deviceUid.isEmpty())
            return;   // no vehicles row to key the flight to
        m_start = start;
        m_flightId = m_db.openFlightRecord(m_connId, m_deviceUid, start.batterySerial, start.payloadKg,
                                           start.armSource, start.gateAllowed, start.mode, start.bench);
        m_flightStartMs = MonotonicClock::nowMs();
        const Sample s = sample();
        m_startSample = s;
        m_startMah = s.mahConsumed;
        m_minVoltage = std::numeric_limits<double>::quiet_NaN();
        noteVoltage(s.voltage);   // ignores 0 / NaN: no battery reading yet
        m_maxAltitude = s.altitudeM;
        m_maxVoltage = s.voltage;
        // Power-type tracking. A flight joined already armed started before
        // the GCS saw it, so its charger number would cover time the tracker
        // never saw: it is recorded but never counts as started full.
        m_armVoltage = start.armSource == kArmSourceAlreadyArmed ? -1.0 : s.voltage;
        if (m_flightId > 0 && powerTracking()) {
            m_tracker.start(start.targetLat, start.targetLon);
            m_lastPowerTickMs = MonotonicClock::nowMs();
            m_powerTimer.start();
        }
        // The pack as it was at arm; editing it later changes no flight.
        if (m_flightId > 0) {
            m_db.openFlightBattery(m_flightId, 1, start.pack, s.voltage, s.percent);
            // The row Flight History, exports and stats read.
            m_historyId = m_db.openHistoryFlight(m_flightId, start.operatorId, start.vehicleSysId,
                                                 start.mode.toUpper(),
                                                 QStringLiteral("arm source: %1%2").arg(start.armSource,
                                                     start.bench ? QStringLiteral(", bench") : QString()),
                                                 start.weatherSummary);
            // Flight History's events: the arm, and who armed.
            if (m_historyId > 0)
                logHistoryEvent(QStringLiteral("ARM"), start.armSource, s);
        }
        updateContext();
    }

    /// Disarmed transition. Returns what was closed (flightId -1 if nothing).
    FlightEnd disarmed()
    {
        return m_flightId > 0 ? closeFlight(m_linkLost ? QStringLiteral("link_lost")
                                                       : QStringLiteral("disarmed"))
                              : FlightEnd{};
    }

    int historyFlightId() const { return m_historyId; }

    /// Trainer handover (instructor's trainer switch, via a mixed channel).
    /// Logged on the open flight (and its Flight History row); before arm it
    /// is a connection event only.
    void trainerHandover(bool studentInControl, const QString &source)
    {
        const QString event = studentInControl ? QStringLiteral("HANDOVER_TO_STUDENT")
                                               : QStringLiteral("HANDOVER_TO_INSTRUCTOR");
        const Sample s = sample();
        if (m_flightId > 0)
            noteAltitude(s.altitudeM);
        m_db.logSessionEvent(m_connId, m_flightId, event, source);
        if (m_historyId > 0)
            m_db.insertTelemetryEvent(m_historyId, event, source,
                                      std::isfinite(s.voltage) ? s.voltage : 0.0,
                                      std::isfinite(s.altitudeM) ? s.altitudeM : 0.0,
                                      qMax(0, s.gpsSats), s.flightMode);
    }

    void linkLost()
    {
        if (m_linkLost)
            return;
        m_linkLost = true;
        m_linkLostAtMs = MonotonicClock::nowMs();
        m_linkLostAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        m_db.logSessionEvent(m_connId, m_flightId, QStringLiteral("link_lost"), QString());
    }

    /// The link is back. Disarmed while it was gone: the flight closes as
    /// link_lost, at the moment the link was lost.
    void linkRestored(bool vehicleArmed)
    {
        if (!m_linkLost)
            return;
        m_db.logSessionEvent(m_connId, m_flightId, QStringLiteral("link_restored"),
                             vehicleArmed ? QStringLiteral("armed") : QStringLiteral("disarmed"));
        if (m_flightId > 0 && !vehicleArmed)
            closeFlight(QStringLiteral("link_lost"));
        m_linkLost = false;
    }

    /// The 10 s running summary (the timer calls it; tests call it directly).
    void writeSummary()
    {
        if (m_connId > 0)
            m_db.touchConnectionSession(m_connId);
        if (m_flightId <= 0 || m_linkLost)
            return;   // during link loss the last summary stays the crash point
        const Sample s = sample();
        adoptMissingBaseline(s);
        noteVoltage(s.voltage);
        noteAltitude(s.altitudeM);
        m_db.updateFlightRecordSummary(m_flightId, elapsedSec(MonotonicClock::nowMs()), m_minVoltage,
                                       mahUsed(s.mahConsumed));
        writePowerRecord(s);
    }

    /// One power-type tick (the 1 s timer calls it; tests call it directly).
    void powerTick(double dtSec)
    {
        if (m_flightId <= 0 || !powerTracking())
            return;
        const Sample s = sample();
        PowerTypeModel::TrackSample t;
        t.groundSpeedMs = s.groundSpeedMs;
        t.altitudeM = s.altitudeM;
        t.currentA = s.currentA;
        t.throttle = s.throttle;
        t.lat = s.lat;
        t.lon = s.lon;
        t.linkUp = !m_linkLost;
        m_tracker.tick(dtSec, t);
    }

    const PowerTypeModel::FlightTracker &powerTracker() const { return m_tracker; }

private:
    Sample sample() const { return m_sample ? m_sample() : Sample{}; }

    /// A counter that was unknown when the flight opened starts counting from
    /// the first sample that has it. That happens when the flight opens
    /// before the first BATTERY_STATUS: a flight joined right after the GCS
    /// started, or a battery monitor that starts reporting late. Without
    /// this, energy, mAh and distance would stay unknown for the whole
    /// flight; with it they cover the flight from that first sample on (at
    /// most one summary interval is missed).
    void adoptMissingBaseline(const Sample &s)
    {
        if (m_startSample.energyWh < 0 && s.energyWh >= 0)
            m_startSample.energyWh = s.energyWh;
        if (m_startMah < 0 && s.mahConsumed >= 0)
            m_startMah = m_startSample.mahConsumed = s.mahConsumed;
        if (m_startSample.distanceM < 0 && s.distanceM >= 0)
            m_startSample.distanceM = s.distanceM;
        if (!(m_startSample.voltage > 0) && s.voltage > 0)
            m_startSample.voltage = s.voltage;
    }

    void noteVoltage(double v)
    {
        if (std::isfinite(v) && v > 0 && (!std::isfinite(m_minVoltage) || v < m_minVoltage))
            m_minVoltage = v;
    }

    void noteAltitude(double alt)
    {
        if (std::isfinite(alt) && (!std::isfinite(m_maxAltitude) || alt > m_maxAltitude))
            m_maxAltitude = alt;
    }

    double mahUsed(double nowMah) const
    {
        return (m_startMah >= 0 && nowMah >= m_startMah) ? nowMah - m_startMah : -1;
    }

    double elapsedSec(qint64 untilMs) const { return qMax<qint64>(0, untilMs - m_flightStartMs) / 1000.0; }

    FlightEnd closeFlight(const QString &reason)
    {
        const bool atLinkLoss = reason == QLatin1String("link_lost") && m_linkLostAtMs >= 0 && m_linkLost;
        const Sample s = sample();
        adoptMissingBaseline(s);
        if (!atLinkLoss)
            noteVoltage(s.voltage);
        FlightEnd end;
        end.flightId = m_flightId;
        end.deviceUid = m_deviceUid;
        end.batterySerial = m_start.batterySerial;
        end.bench = m_start.bench;
        end.endReason = reason;
        end.durationSec = elapsedSec(atLinkLoss ? m_linkLostAtMs : MonotonicClock::nowMs());
        end.mahUsed = mahUsed(s.mahConsumed);
        end.whUsed = energyUsedWh(m_startSample.energyWh, s.energyWh, end.mahUsed, m_startSample.voltage, s.voltage);
        m_db.closeFlightRecord(m_flightId, end.durationSec, reason, atLinkLoss ? m_linkLostAtUtc : QString(),
                               m_minVoltage, end.mahUsed);
        m_db.closeFlightBattery(m_flightId, 1, s.voltage, s.percent, m_minVoltage, end.mahUsed, end.whUsed);
        // Energy and distance from measurements only; unknown stays unset.
        const double distM = (m_startSample.distanceM >= 0 && s.distanceM >= m_startSample.distanceM)
                ? s.distanceM - m_startSample.distanceM : -1;
        if (end.whUsed >= 0 || distM >= 0)
            m_db.updateFlightSessionEnergy(m_flightId, qMax(0.0, end.whUsed), qMax(0.0, distM));
        writePowerRecord(s);
        m_powerTimer.stop();
        if (m_historyId > 0) {
            if (!atLinkLoss)
                noteAltitude(s.altitudeM);
            logHistoryEvent(QStringLiteral("DISARM"), reason, s);
            m_db.setFlightDisarmedAt(m_historyId, QDateTime::currentDateTimeUtc());
            m_db.closeFlight(m_historyId, qRound(end.durationSec),
                             std::isfinite(m_maxAltitude) ? m_maxAltitude : 0.0,
                             std::isfinite(m_minVoltage) ? m_minVoltage : 0.0,
                             std::isfinite(m_maxVoltage) ? m_maxVoltage : 0.0, 0,
                             0.0, 0.0, qMax(0.0, distM));
        }
        m_historyId = -1;
        m_flightId = -1;
        m_startMah = -1;
        m_minVoltage = std::numeric_limits<double>::quiet_NaN();
        updateContext();
        if (m_onFlightClosed)
            m_onFlightClosed(end);
        return end;
    }

    void updateContext() { m_db.setRecordingContext(m_connId, m_flightId, m_deviceUid, m_historyId); }

    /// One event on the flight's Flight History row, with the vehicle's
    /// state at that moment.
    void logHistoryEvent(const QString &event, const QString &detail, const Sample &s)
    {
        m_db.insertTelemetryEvent(m_historyId, event, detail,
                                  std::isfinite(s.voltage) ? s.voltage : 0.0,
                                  std::isfinite(s.altitudeM) ? s.altitudeM : 0.0,
                                  qMax(0, s.gpsSats), s.flightMode);
    }

    /// Bench arms (motor/servo tests, Testing mode) teach the model nothing.
    bool powerTracking() const { return m_start.powerTypeId > 0 && !m_start.bench; }

    void writePowerRecord(const Sample &s)
    {
        if (m_flightId <= 0 || !powerTracking())
            return;
        FlightPowerRecord r;
        r.powerTypeId = m_start.powerTypeId;
        r.hoverMinEmpty = m_tracker.hoverMinEmpty();
        r.hoverMinLoaded = m_tracker.hoverMinLoaded();
        r.cruiseMinEmpty = m_tracker.cruiseMinEmpty();
        r.cruiseMinLoaded = m_tracker.cruiseMinLoaded();
        r.cruiseSpeedMs = m_tracker.cruiseSpeedMs();
        r.fcMah = mahUsed(s.mahConsumed);
        r.synthHoverA = m_tracker.hoverCurrentA();
        r.synthCruiseA = m_tracker.cruiseCurrentA();
        r.refThrottleHover = m_tracker.hoverThrottle();
        r.refThrottleCruise = m_tracker.cruiseThrottle();
        r.armCellV = (m_armVoltage > 0 && m_start.cells > 0) ? m_armVoltage / m_start.cells : -1.0;
        r.startedFull = m_armVoltage > 0 && PowerTypeModel::fullAtArm(m_armVoltage, m_start.cells);
        r.telemetryCoverage = m_tracker.coverage();
        r.timeAtTargetMin = m_start.timeAtTargetMin;
        m_db.saveFlightPowerRecord(m_flightId, r);
    }

    DatabaseManager &m_db;
    std::function<Sample()> m_sample;
    std::function<void(const FlightEnd &)> m_onFlightClosed;
    QTimer m_summaryTimer;
    QTimer m_powerTimer;
    PowerTypeModel::FlightTracker m_tracker;
    qint64 m_lastPowerTickMs = MonotonicClock::kNever;
    double m_armVoltage = -1;      ///< pack voltage at arm; -1 = unknown or joined mid-flight

    int m_connId = -1;
    QString m_deviceUid;
    int m_flightId = -1;
    FlightStart m_start;
    Sample m_startSample;
    qint64 m_flightStartMs = MonotonicClock::kNever;
    double m_startMah = -1;
    double m_minVoltage = std::numeric_limits<double>::quiet_NaN();
    bool m_linkLost = false;
    qint64 m_linkLostAtMs = MonotonicClock::kNever;
    QString m_linkLostAtUtc;
    int m_historyId = -1;          ///< flights row (Flight History)
    double m_maxAltitude = std::numeric_limits<double>::quiet_NaN();
    double m_maxVoltage = std::numeric_limits<double>::quiet_NaN();
};
