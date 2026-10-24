#pragma once
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "QGCPalette.h"
#include "Vehicle/Vehicle.h"
#include <QTimer>
#include <QPointer>
#include "core/TrainerSwitchTracker.h"

/// @file PreflightPlugin.h
///
/// Main plugin entry point for the Skywin GCS preflight system.
/// Subclasses QGCCorePlugin to inject custom UI, managers, and logic into
/// the QGroundControl framework. This is the "glue" class that:
///   - Owns all custom managers (PreflightManager, TelemetryBridge, ArmingGate, etc.)
///   - Exposes them to the QML UI via context properties and singletons
///   - Wires vehicle lifecycle signals to setup/teardown logic
///   - Manages flight sessions (gate open → gate close) and compliance logging
///   - Provides the custom dark theme via paletteOverride()
///
/// Instantiated as a singleton via Q_APPLICATION_STATIC; QGroundControl discovers
/// it through the plugin system.

class QmlComponentInfo;
class PreflightManager;
class PreflightChecklistModel;
class PreflightChecklistFilterModel;
class TelemetryBridge;
class ArmingGate;
class WeatherProvider;
class PreflightSettingsManager;
class VehicleProfileManager;
class PowerModel;
class ExportHelper;
class HardwareTestController;
class ControlSurfaceTestController;
class Vehicle;
class ChecklistEngine;
class ChecklistItemModel;
class VehicleRegistry;
class NoFlyZoneModel;
class SessionRecorder;
class VehicleListModel;
class MissionController;

/// The single source of the vehicle's armed
/// transitions for the preflight side. Armed state comes only from
/// Vehicle::armedChanged (the autopilot's HEARTBEAT); COMMAND_ACKs only label
/// WHO armed (arm source) and never change armed state. On every transition
/// it emits ArmingGate::vehicleArmed/vehicleDisarmed (gate and FSM resets, so
/// an RC disarm clears an unspent acknowledgment) and gateOpened/gateClosed
/// (SessionRecorder opens and closes the flight).
class ArmStateTracker : public QObject
{
    Q_OBJECT
public:
    enum class ArmSource { External = 0, Gcs, ForceToken };
    Q_ENUM(ArmSource)

    /// Window in which an accepted arm ACK attributes the next arm to this GCS.
    static constexpr qint64 kGcsAckWindowMs = 3000;

    explicit ArmStateTracker(ArmingGate *gate, QObject *parent = nullptr);

    /// Follow @p vehicle's armedChanged (nullptr stops following).
    void setVehicle(Vehicle *vehicle);
    /// An ARM_DISARM COMMAND_ACK arrived. Labels the arm source only.
    void onArmDisarmAck(bool accepted);
    /// Vehicle armed state changed (from the HEARTBEAT). Public for tests.
    void onArmedChanged(bool armed);

    ArmSource lastArmSource() const { return m_lastArmSource; }
    /// Pure rule: force window open → ForceToken; an accepted ack no older
    /// than kGcsAckWindowMs → Gcs; otherwise External (RC or another peer).
    static ArmSource sourceFor(qint64 gcsAckAgeMs, bool forceWindowOpen);

private:
    ArmingGate *m_gate = nullptr;
    QPointer<Vehicle> m_vehicle;
    QMetaObject::Connection m_armedConn;
    bool m_armed = false;
    qint64 m_lastGcsAckMs = -1;   ///< MonotonicClock ms of the last accepted ack while disarmed
    qint64 m_armedAtMs = -1;      ///< MonotonicClock ms of the last arm transition
    ArmSource m_lastArmSource = ArmSource::External;
};

class PreflightPlugin : public QGCCorePlugin
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_PROPERTY(double fontSizeFactor READ fontSizeFactor CONSTANT)
    /// Trainer handover (two-radio training): "instructor", "student" or
    /// "unmonitored" (no valid value on the trainer channel).
    Q_PROPERTY(QString trainerControl READ trainerControl NOTIFY trainerChanged)
    /// The trainer channel has moved at least once this connection.
    Q_PROPERTY(bool trainerVerified READ trainerVerified NOTIFY trainerChanged)
    /// RC channel the instructor's radio mixes the trainer switch onto (default 8).
    Q_PROPERTY(int trainerChannel READ trainerChannel WRITE setTrainerChannel NOTIFY trainerChanged)
    /// true (default): trainer switch (SH) high = instructor / master flying; low = trainee.
    Q_PROPERTY(bool trainerHighIsInstructor READ trainerHighIsInstructor WRITE setTrainerHighIsInstructor NOTIFY trainerChanged)
    /// "Detect": while running, the operator flicks SH and the channel is found.
    Q_PROPERTY(bool trainerDetectRunning READ trainerDetectRunning NOTIFY trainerDetectChanged)
    Q_PROPERTY(int trainerDetectSecondsLeft READ trainerDetectSecondsLeft NOTIFY trainerDetectChanged)
    /// The channel found (-1 = none); acceptTrainerDetect() makes it the trainer channel.
    Q_PROPERTY(int trainerDetectFound READ trainerDetectFound NOTIFY trainerDetectChanged)
    /// What to show: the instruction while running, then the result.
    Q_PROPERTY(QString trainerDetectStatus READ trainerDetectStatus NOTIFY trainerDetectChanged)
    /// Non-empty when the trainer channel cannot be the trainer switch (it is FLTMODE_CH).
    Q_PROPERTY(QString trainerChannelConflict READ trainerChannelConflict NOTIFY trainerChanged)
public:
    // Font scaling factor used by QML to scale UI elements proportionally.
    Q_INVOKABLE double fontSizeFactor() const { return 1.95; }

    explicit PreflightPlugin(QObject *parent = nullptr);
    ~PreflightPlugin();

    static PreflightPlugin *instance();

    void init() override;

    QString trainerControl() const { return TrainerSwitchTracker::name(_trainer.control()); }
    bool trainerVerified() const { return _trainer.verified(); }
    int trainerChannel() const { return _trainer.channel(); }
    void setTrainerChannel(int channel);
    QString trainerChannelConflict() const;
    bool trainerHighIsInstructor() const { return _trainer.highIsInstructor(); }
    void setTrainerHighIsInstructor(bool v);
    /// The stored "SH high = instructor" box: only "1" means ticked. Unset is
    /// the default for the operator's radios, SH low = instructor.
    static bool trainerHighIsInstructorFromSetting(const QString &stored) { return stored == QLatin1String("1"); }
    bool trainerDetectRunning() const { return _detectRunning; }
    int trainerDetectSecondsLeft() const { return _detectSecsLeft; }
    int trainerDetectFound() const { return _detectFound; }
    QString trainerDetectStatus() const { return _detectStatus; }
    Q_INVOKABLE void startTrainerDetect();
    Q_INVOKABLE void cancelTrainerDetect();
    Q_INVOKABLE void acceptTrainerDetect();

signals:
    void trainerChanged();
    void trainerDetectChanged();
    /// A handover just happened in a Training session (shown as a toast).
    void trainerHandoverAnnounced(const QString &text, bool student);

public:

    const QVariantList &analyzePages() override;
    const QVariantList &toolBarIndicators() override;

    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) override;
    void createRootWindow(QQmlApplicationEngine *qmlEngine) override;

    QString brandImageIndoor() const override { return QStringLiteral("qrc:/custom/brand/logo"); }
    QString brandImageOutdoor() const override { return QStringLiteral("qrc:/custom/brand/logo"); }

    void paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo) override;
    bool adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData) override;

    bool mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message) override;

private slots:
    void _onActiveVehicleChanged(Vehicle *vehicle);
    void _onKnownVehicleConnected(int vehicleId);
    void _onGateOpened();
    void _onGateClosed(const QString &reason);
    void _refreshWeather();

private:
    void _setupForVehicle(Vehicle *vehicle);
    void _startWeatherRefresh();
    void _populateChecklistModel();
    void _wireZoneComplianceCheck();
    void _resolvePlanMissionController(QQmlApplicationEngine *qmlEngine);
    void _surfaceCommandRejection(const QString &command, const QString &reason);
    static QString _mavResultToString(int result);

    // --- Owned managers and models (all parented to this for cleanup) ---
    QVariantList _analyzePages;                // Lazily-built list of Analyze tab pages
    QVariantList _toolBarIndicators;           // Lazily-built list of toolbar indicator QML URLs
    PreflightManager *_preflightManager = nullptr;          // Runs preflight checks and evaluation cycles
    PreflightChecklistModel *_checklistModel = nullptr;     // Data model for all checks (source model)
    PreflightChecklistFilterModel *_categoryModels[8] = {}; // Per-category proxy models (0-7) filtering _checklistModel
    TelemetryBridge *_telemetryBridge = nullptr;            // Bridges MAVLink telemetry to Qt properties for QML binding
    ArmingGate *_armingGate = nullptr;                      // Manages arming gate logic and emits gateOpened/gateClosed
    WeatherProvider *_weatherProvider = nullptr;             // Fetches METAR/weather data by ICAO or coordinates
    PreflightSettingsManager *_preflightSettingsManager = nullptr; // Exposes plugin settings to QML
    ExportHelper *_exportHelper = nullptr;                  // Handles data export (logs, compliance, etc.)
    PowerModel *_powerModel = nullptr;                      // Battery/power estimation model
    HardwareTestController *_hardwareTestController = nullptr;     // Controls hardware test sequences (servo, motor)
    ControlSurfaceTestController *_controlSurfaceTestController = nullptr; // Controls surface sweep tests
    VehicleProfileManager *_vehicleProfileManager = nullptr;      // Loads/saves per-vehicle configuration profiles
    ChecklistItemModel *_checklistItemModel = nullptr;      // Flattened checklist items for the QML ChecklistEngine
    ChecklistEngine *_checklistEngine = nullptr;            // Drives checklist evaluation using TelemetryBridge data
    NoFlyZoneModel *_noFlyZoneModel = nullptr;              // Airspace knowledge base + manual compliance model
    VehicleListModel *_vehicleListModel = nullptr;          // Vehicle registry list for the Vehicles page
    MissionController *_planMissionController = nullptr;    // Plan View's mission controller (zone compliance source)

    // --- Flight session tracking ---
    SessionRecorder *_sessionRecorder = nullptr;  // the only session/flight writer
    TrainerSwitchTracker _trainer;                // who has control in two-radio training
    void _onRcChannels();
    TrainerChannelDetector _detector;             // "Detect" on the trainer chip
    QTimer _detectTimer;                          // 1 s countdown while detecting
    bool _detectRunning = false;
    int _detectSecsLeft = 0;
    int _detectFound = -1;
    QString _detectStatus;
    int _flightModeChannel() const;               // FLTMODE_CH, -1 if unknown
    void _finishTrainerDetect();
    QMetaObject::Connection _linkLostConn;        // current vehicle's communicationLostChanged
    void _openConnectionSession();                // opens once the vehicle UID is known
    /// Flight controller's mAh-consumed counter for battery 1, -1 when unavailable.
    double _batteryMahConsumed() const;
    QTimer _weatherRefreshTimer;      // Periodic timer that triggers weather data refresh
    ArmStateTracker *_armStateTracker = nullptr; // Armed transitions from HEARTBEAT only
};
