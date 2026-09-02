#pragma once
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "QGCPalette.h"
#include <QTimer>

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
class VehicleListModel;
class MissionController;

class PreflightPlugin : public QGCCorePlugin
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_PROPERTY(double fontSizeFactor READ fontSizeFactor CONSTANT)
public:
    // Font scaling factor used by QML to scale UI elements proportionally.
    Q_INVOKABLE double fontSizeFactor() const { return 1.95; }

    explicit PreflightPlugin(QObject *parent = nullptr);
    ~PreflightPlugin();

    static PreflightPlugin *instance();

    void init() override;

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
    void _onNewVehicleRegistered(int vehicleId);
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
    int _currentSessionId = -1;       // Active DB flight session ID, -1 when no session is active
    QDateTime _sessionStartTime;      // Timestamp when gateOpened started the session
    QTimer _weatherRefreshTimer;      // Periodic timer that triggers weather data refresh
    int _lastArmDisarmParam = -1;     // 1=arm, 0=disarm, -1=unknown from last COMMAND_LONG
};
