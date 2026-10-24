#include "PreflightPlugin.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFontDatabase>
#include <QJSEngine>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtCore/QApplicationStatic>

#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QmlObjectListModel.h"
#include "QmlComponentInfo.h"
#include "Vehicle/Vehicle.h"
#include "Vehicle/FactGroups/VehicleBatteryFactGroup.h"
#include "AppSettings.h"
#include "FactMetaData.h"

Q_DECLARE_METATYPE(Vehicle*)

// QGroundControl QML module registrations are declared below and called in the
// constructor to force-link them from static libraries. Without explicit references,
// the linker discards the QQmlModuleRegistration globals.
void qml_register_types_QGroundControl();
void qml_register_types_QGroundControl_AnalyzeView();
void qml_register_types_QGroundControl_AppSettings();
void qml_register_types_QGroundControl_AutoPilotPlugins_Common();
void qml_register_types_QGroundControl_Controls();
void qml_register_types_QGroundControl_FactControls();
void qml_register_types_QGroundControl_FactSystem();
void qml_register_types_QGroundControl_FlightDisplay();
void qml_register_types_QGroundControl_FlightMap();
void qml_register_types_QGroundControl_MainWindow();
void qml_register_types_QGroundControl_ScreenTools();
void qml_register_types_QGroundControl_Toolbar();
void qml_register_types_QGroundControl_Vehicle();
void qml_register_types_QGroundControl_VehicleSetup();
int qInitResources_qmlcache_AnalyzeViewModule();
int qInitResources_qmlcache_AppSettingsModule();
int qInitResources_qmlcache_FactControlsModule();
int qInitResources_qmlcache_MainWindowModule();
int qInitResources_qmlcache_QGroundControlControlsModule();

int qInitResources_qmlcache_VehicleSetupModule();

#include "AbstractCheck.h"
#include "adapters/TelemetryBridge.h"
#include "core/ArmingGate.h"
#include "core/BatteryPackCheck.h"
#include "core/SessionRecorder.h"
#include "Vehicle/VehicleLinkManager.h"
#include "core/ChecklistEngine.h"
#include "core/ChecklistItemModel.h"
#include "core/PowerModel.h"
#include "core/VehicleProfileManager.h"
#include "utils/DatabaseManager.h"
#include "PreflightChecklistFilterModel.h"
#include "PreflightChecklistModel.h"
#include "PreflightManager.h"
#include "PreflightSettingsManager.h"
#include "controllers/HardwareTestController.h"
#include "controllers/ControlSurfaceTestController.h"
#include "utils/Config.h"
#include "detection/VehicleRegistry.h"
#include "core/AbstractCheck.h"
#include "utils/ExportHelper.h"
#include "utils/WeatherProvider.h"
#include "managers/OperatorManager.h"
#include "managers/FlightSession.h"
#include "managers/TelemetryEventLogger.h"
#include "models/FlightHistoryModel.h"
#include "models/NoFlyZoneModel.h"
#include "models/VehicleListModel.h"
#include "core/ZoneComplianceCheck.h"
#include "MissionManager/MissionController.h"
#include "MissionManager/PlanMasterController.h"
#include "utils/MonotonicClock.h"

Q_LOGGING_CATEGORY(preflightPluginLog, "preflight.plugin")

Q_APPLICATION_STATIC(PreflightPlugin, _preflightPluginInstance)

extern int qInitResources_custom();

// Singleton access — returned via Q_APPLICATION_STATIC so only one instance exists.
PreflightPlugin::PreflightPlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
    // Force-link all QML module type registrations from static libraries.
    // These calls ensure the linker includes the registration code; the actual
    // types become available when the QML engine is created later in init().
    qml_register_types_QGroundControl();
    qml_register_types_QGroundControl_FlightDisplay();
    qml_register_types_QGroundControl_Controls();
    qml_register_types_QGroundControl_FlightMap();
    qml_register_types_QGroundControl_Toolbar();
    qml_register_types_QGroundControl_VehicleSetup();
    qml_register_types_QGroundControl_FactControls();
    qml_register_types_QGroundControl_AppSettings();
    qml_register_types_QGroundControl_FactSystem();
    qml_register_types_QGroundControl_MainWindow();
    qml_register_types_QGroundControl_ScreenTools();
    qml_register_types_QGroundControl_Vehicle();
    qInitResources_qmlcache_AnalyzeViewModule();
    qInitResources_qmlcache_VehicleSetupModule();
    qInitResources_qmlcache_FactControlsModule();
    qInitResources_qmlcache_AppSettingsModule();
    qInitResources_qmlcache_QGroundControlControlsModule();
    qInitResources_qmlcache_MainWindowModule();
    // Register Vehicle with QML so Q_PROPERTY(Vehicle* ...) with REQUIRED works
    qmlRegisterUncreatableType<Vehicle>("QGroundControl", 1, 0, "Vehicle", QStringLiteral("Cannot create Vehicle from QML"));

    // Register custom resources last to ensure they override any duplicate stock paths
    qInitResources_custom();

    qCDebug(preflightPluginLog) << "PreflightPlugin: Constructed";
}

PreflightPlugin::~PreflightPlugin()
{
    // Close the session records now, while the objects the recorder's
    // callbacks use (telemetry bridge, profile manager, power model) are still
    // alive. Left to the recorder's own destructor, an exit while armed would
    // run them after those children were deleted.
    if (_sessionRecorder) {
        _sessionRecorder->connectionClosed(QStringLiteral("app_exit"));
        _sessionRecorder->setSampleProvider({});
        _sessionRecorder->setFlightClosedHandler({});
    }
    qCDebug(preflightPluginLog) << "PreflightPlugin: Destroyed";
}

PreflightPlugin *PreflightPlugin::instance()
{
    return _preflightPluginInstance();
}

// ---------------------------------------------------------------------------
// init() — Called once after plugin construction. Sets up fonts, creates all
// managers, wires signals, and connects to any already-active vehicle.
// ---------------------------------------------------------------------------
void PreflightPlugin::init()
{
    QGCCorePlugin::init();

    // Load custom application font and apply it globally
    int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Abel-Regular"));
    if (fontId < 0) {
        qCWarning(preflightPluginLog) << "Could not load Abel-Regular font";
    } else {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            QFont appFont(families.first());
            appFont.setPixelSize(12);
            qApp->setFont(appFont);
            qCDebug(preflightPluginLog) << "Set application font to" << families.first();
        }
    }

    // --- Create core managers ---
    // PreflightManager owns the check definitions; the checklist model wraps them for QML.
    _preflightManager = new PreflightManager(1, this);
    _checklistModel = new PreflightChecklistModel(this);
    _checklistModel->setPreflightManager(_preflightManager);
    _telemetryBridge = new TelemetryBridge(this);

    // ArmingGate and ChecklistEngine must exist before any wiring below refers
    // to them (TelemetryEventLogger::setDependencies and the FlightSession
    // connects take real pointers, not nullptr).
    _armingGate = new ArmingGate(this);
    _armStateTracker = new ArmStateTracker(_armingGate, this);
    _armingGate->setPreflightManager(_preflightManager);
    _armingGate->setTelemetryBridge(_telemetryBridge);
    _telemetryBridge->setArmingGate(_armingGate);
    _preflightManager->setTelemetryBridge(_telemetryBridge);

    _checklistItemModel = new ChecklistItemModel(this);
    _populateChecklistModel();
    _checklistEngine = new ChecklistEngine(this);
    _checklistEngine->setModel(_checklistItemModel);
    _checklistEngine->setTelemetryBridge(_telemetryBridge);

    // OperatorManager is QML_SINGLETON — instantiation registers it
    new OperatorManager(this);
    new FlightSession(this);
    auto *telemetryLogger = new TelemetryEventLogger(this);
    telemetryLogger->setDependencies(_telemetryBridge, _armingGate, _checklistEngine);

    // Wire ArmingGate signals to FlightSession
    connect(_armingGate, &ArmingGate::vehicleArmed,
            FlightSession::instance(), &FlightSession::onVehicleArmed);
    connect(_armingGate, &ArmingGate::vehicleDisarmed,
            FlightSession::instance(), &FlightSession::onVehicleDisarmed);

    // Drive the preflight FSM's Armed terminal from confirmed arm/disarm so the
    // "Ready to Arm"/"Armed" QML state label matches the real vehicle.
    connect(_armingGate, &ArmingGate::vehicleArmed,
            _preflightManager, &PreflightManager::markArmed);
    connect(_armingGate, &ArmingGate::vehicleDisarmed,
            _preflightManager, &PreflightManager::markDisarmed);

    // Post-flight is parked for Phase 1 (Config.h kPostFlightEnabled): every
    // disarm re-runs preflight from Pending instead of marking every check
    // Skipped (which allMandatoryPassed() counted as passed).
    if (kPostFlightEnabled) {
        connect(FlightSession::instance(), &FlightSession::postFlightChecklistRequired,
                _preflightManager, &PreflightManager::activatePostFlightChecks);
    } else {
        connect(FlightSession::instance(), &FlightSession::postFlightChecklistRequired,
                _preflightManager, &PreflightManager::resetForNextFlight);
    }

    // Create one proxy model per checklist category (8 max) so QML can bind
    // each category section to its own filtered view of the source checklist.
    for (int i = 0; i < 8; ++i) {
        _categoryModels[i] = new PreflightChecklistFilterModel(this);
        _categoryModels[i]->setSourceModel(static_cast<QAbstractItemModel *>(_checklistModel));
        _categoryModels[i]->setCategoryId(i);
    }

    _weatherProvider = new WeatherProvider(this);
    _preflightSettingsManager = new PreflightSettingsManager(this);
    connect(&_weatherRefreshTimer, &QTimer::timeout, this, &PreflightPlugin::_refreshWeather);

    _hardwareTestController = new HardwareTestController(this);
    _controlSurfaceTestController = new ControlSurfaceTestController(this);
    // Bench-test controllers open a bounded, logged gate override before their
    // sanctioned arm commands; hand them the same gate as the telemetry bridge.
    _hardwareTestController->setArmingGate(_armingGate);
    _controlSurfaceTestController->setArmingGate(_armingGate);
    // Force-arm from either bench controller must go through the single funnel
    // (TelemetryBridge::forceArm): one bounded override, one 2989-param2 send.
    _hardwareTestController->setTelemetryBridge(_telemetryBridge);
    _controlSurfaceTestController->setTelemetryBridge(_telemetryBridge);

    // The NoFlyZoneModel constructor reloads zones from the database, so the
    // DB must be initialized before the model is created or the dropdown will
    // always be empty.
    // A database that cannot open or migrate means nothing is logged.
    // The Fly view keeps a banner up from Database.startupError; this message
    // tells the operator once at startup.
    if (!DatabaseManager::instance().initialize()) {
        qgcApp()->showAppMessage(tr("Flight logging is OFF: %1")
                                     .arg(DatabaseManager::instance().startupError()));
    }

    _noFlyZoneModel = new NoFlyZoneModel(this);
    _vehicleListModel = new VehicleListModel(this);

    // The zone-compliance check was registered during PreflightManager
    // construction (before the model existed) — wire it up now.
    _wireZoneComplianceCheck();

    _powerModel = new PowerModel(this);
    _exportHelper = new ExportHelper(this);

    // Application branding used by the title bar and OS
    QCoreApplication::setApplicationName(QStringLiteral("Skywin GCS"));

    _vehicleProfileManager = new VehicleProfileManager(this);
    _vehicleProfileManager->setTelemetryBridge(_telemetryBridge);
    _vehicleProfileManager->setArmingGate(_armingGate);

    // Keep blocking rules in sync with the resolved vehicle kind (quad vs
    // fixed wing). Fires on connect and whenever the type re-resolves.
    connect(_vehicleProfileManager, &VehicleProfileManager::vehicleTypeResolved,
            _preflightManager, [this]() {
        if (_vehicleProfileManager && _preflightManager) {
            _preflightManager->applyVehicleKind(_vehicleProfileManager->vehicleKindString());
        }
    });

    // The operator names the fitted pack ("Battery pack" in the Power
    // section). The check reads the selection through this lambda: the pack's
    // registry row, plus the FC's BATT_CAPACITY once parameters have loaded,
    // so the check can warn when the two disagree. No serial is ever filled in
    // automatically, since that would satisfy the requirement without anyone
    // choosing a pack.
    auto *packCheck = new BatteryPackCheck([this]() -> QVariantMap {
        const QString serial = _vehicleProfileManager ? _vehicleProfileManager->currentBatterySerial() : QString();
        if (serial.isEmpty())
            return {};
        QVariantMap pack = DatabaseManager::instance().batteryPack(serial);
        if (pack.isEmpty())
            pack[QStringLiteral("serial")] = serial;   // database off: still selected
        if (_telemetryBridge && _telemetryBridge->hasParameter(QStringLiteral("BATT_CAPACITY")))
            pack[QStringLiteral("fcCapacityMah")] =
                qRound(_telemetryBridge->parameterValue(QStringLiteral("BATT_CAPACITY"), 0.0f));
        return pack;
    }, _preflightManager);
    _preflightManager->addCheck(packCheck);
    connect(_vehicleProfileManager, &VehicleProfileManager::currentBatteryChanged,
            packCheck, [packCheck]() { packCheck->evaluate(); });
    // Action checks are not re-run on the evaluation timer: re-check once the
    // vehicle's parameters (BATT_CAPACITY) have loaded.
    connect(_telemetryBridge, &TelemetryBridge::parametersReadyChanged,
            packCheck, [packCheck](bool) { packCheck->evaluate(); });

    // --- Vehicle registry signals ---
    // knownVehicleConnected: a previously-seen vehicle reconnected; load its saved config.
    auto *registry = VehicleRegistry::instance();
    connect(registry, &VehicleRegistry::knownVehicleConnected,
            this, &PreflightPlugin::_onKnownVehicleConnected);

    // Gate open/close tracks the flight session lifecycle for compliance logging.
    // A refusal is never silent. Gate denials from requestArm() and
    // autopilot rejections (_surfaceCommandRejection) both arrive here.
    connect(_armingGate, &ArmingGate::commandRejected, this, [](const QString &reason) {
        qgcApp()->showAppMessage(reason);
    });
    // Return reports its real outcome (heartbeat-confirmed or not).
    connect(_telemetryBridge, &TelemetryBridge::rtlCommandCompleted, this, [this](bool confirmed) {
        qgcApp()->showAppMessage(confirmed
            ? tr("Return confirmed: the vehicle reports RTL mode")
            : tr("Return NOT confirmed by the vehicle — check its flight mode"));
    });

    // SessionRecorder is the only writer of connection and
    // flight rows. It opens the connection once the vehicle UID resolves.
    _sessionRecorder = new SessionRecorder(DatabaseManager::instance(), this);
    _sessionRecorder->setSampleProvider([this]() {
        SessionRecorder::Sample s;
        if (_telemetryBridge && _telemetryBridge->vehicle()) {
            Vehicle *v = _telemetryBridge->vehicle();
            s.voltage = _telemetryBridge->batteryVoltage();
            s.mahConsumed = _batteryMahConsumed();
            s.percent = _telemetryBridge->batteryPercent();
            s.energyWh = _telemetryBridge->batteryEnergyConsumedWh();
            const QVariant dist = v->flightDistance()->rawValue();
            s.distanceM = dist.isValid() ? dist.toDouble() : -1;
            s.altitudeM = _telemetryBridge->altitudeRelative();
            s.gpsSats = _telemetryBridge->gpsSatellites();
            s.flightMode = _telemetryBridge->flightMode();
        }
        return s;
    });
    _sessionRecorder->setFlightClosedHandler([this](const SessionRecorder::FlightEnd &end) {
        // Battery cycle: non-bench flights only, >= 20 % of the pack.
        if (!end.bench && end.mahUsed >= 0
            && DatabaseManager::instance().recordBatteryFlight(end.flightId, end.batterySerial, end.mahUsed)
            && _vehicleProfileManager && _vehicleProfileManager->currentBatterySerial() == end.batterySerial)
            _vehicleProfileManager->setBatterySerial(end.batterySerial);   // refreshes the cycle count shown

        // Compliance log entry for the flight (as the gate-close code wrote).
        QJsonObject logEntry;
        logEntry[QStringLiteral("sessionId")] = end.flightId;
        logEntry[QStringLiteral("deviceUid")] = end.deviceUid;
        logEntry[QStringLiteral("durationSec")] = end.durationSec;
        logEntry[QStringLiteral("endReason")] = end.endReason;
        logEntry[QStringLiteral("bench")] = end.bench;
        logEntry[QStringLiteral("endTime")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        DatabaseManager::instance().saveComplianceLog(
            QStringLiteral("session-%1").arg(end.flightId), end.deviceUid, QString(), QString(),
            QString::fromUtf8(QJsonDocument(logEntry).toJson(QJsonDocument::Compact)), QString());

        // Energy-model calibration from this vehicle's recorded flights.
        if (!end.bench && _powerModel) {
            const QVector<QPair<double, double>> history =
                DatabaseManager::instance().getPowerCalibrationHistory(end.deviceUid, 50);
            if (!history.isEmpty()) {
                QVector<PowerCalibrationPoint> points;
                points.reserve(history.size());
                for (const auto &h : history)
                    points.append(PowerCalibrationPoint{h.first, h.second});
                _powerModel->setCalibrationPoints(end.deviceUid, points);
            }
        }
        // VehicleProfileManager writes this flight's energy on the same
        // disarm; release its flight id only after that has run.
        QTimer::singleShot(0, this, [this, id = end.flightId]() {
            if (_vehicleProfileManager && _vehicleProfileManager->currentFlightSessionId() == id)
                _vehicleProfileManager->setFlightSessionId(-1);
        });
        qCInfo(preflightPluginLog) << "Flight" << end.flightId << "closed:" << end.endReason
                                   << end.durationSec << "s" << (end.bench ? "(bench)" : "");
    });
    connect(_vehicleProfileManager, &VehicleProfileManager::currentVehicleChanged,
            this, &PreflightPlugin::_openConnectionSession);

    // Trainer handover: the instructor's radio mixes its trainer switch onto
    // a spare channel (default CH8); RC_CHANNELS carries it here.
    bool chOk = false;
    const int savedChannel = DatabaseManager::instance().getCheckConfig(QStringLiteral("trainer"),
                                                                       QStringLiteral("channel")).toInt(&chOk);
    _trainer.setChannel(chOk && savedChannel >= 1 && savedChannel <= 18 ? savedChannel
                                                                       : TrainerSwitchTracker::kDefaultChannel);
    // On the operator's radios SH low = master / instructor flying and SH high
    // = trainee (measured on the bench), so that is the default; the chip's
    // "SH high = instructor" box stores "1" for a radio wired the other way.
    _trainer.setHighIsInstructor(trainerHighIsInstructorFromSetting(DatabaseManager::instance().getCheckConfig(
        QStringLiteral("trainer"), QStringLiteral("high_is_instructor"))));
    connect(_telemetryBridge, &TelemetryBridge::rcChannelValuesChanged, this, &PreflightPlugin::_onRcChannels);
    _detectTimer.setInterval(1000);
    connect(&_detectTimer, &QTimer::timeout, this, [this]() {
        if (--_detectSecsLeft <= 0)
            _finishTrainerDetect();
        else
            emit trainerDetectChanged();
    });

    connect(_armingGate, &ArmingGate::gateOpened,
            this, &PreflightPlugin::_onGateOpened);
    connect(_armingGate, &ArmingGate::gateClosed,
            this, &PreflightPlugin::_onGateClosed);

    qCDebug(preflightPluginLog) << "PreflightPlugin: init complete,"
                                << _preflightManager->totalChecks() << "checks";

    // Connect to vehicle changes going forward; if a vehicle is already active,
    // set up for it immediately so the UI is ready on startup.
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
            this, &PreflightPlugin::_onActiveVehicleChanged);

    Vehicle *activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (activeVehicle) {
        _setupForVehicle(activeVehicle);
    }
}

// ---------------------------------------------------------------------------
// createQmlApplicationEngine() — Builds the QML engine and registers all
// custom context properties and singleton types that QML views bind to.
// This is the bridge between C++ managers and QML UI.
// ---------------------------------------------------------------------------
QQmlApplicationEngine *PreflightPlugin::createQmlApplicationEngine(QObject *parent)
{
    QQmlApplicationEngine *qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);

    // Add import path for custom QML components (CPTS = custom preflight tools)
    qmlEngine->addImportPath(QStringLiteral("qrc:/qml/cpts"));

    // Register QML singletons — these are global objects accessible from any QML file
    // under the com.uav.preflight module namespace.
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/Colors.qml")), "com.uav.preflight", 1, 0, "Colors");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/Config.qml")), "com.uav.preflight", 1, 0, "Config");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/VehicleTelemetry.qml")), "com.uav.preflight", 1, 0, "VehicleTelemetry");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/DistanceTracker.qml")), "com.uav.preflight", 1, 0, "DistanceTracker");
    qmlRegisterUncreatableType<AbstractCheck>("com.uav.preflight", 1, 0, "AbstractCheck", QStringLiteral("Cannot create AbstractCheck from QML"));
    qmlRegisterSingletonType<PreflightSettingsManager>("com.uav.preflight", 1, 0, "PreflightSettingsManager",
        [](QQmlEngine *engine, QJSEngine *jsEngine) -> QObject * {
            Q_UNUSED(engine) Q_UNUSED(jsEngine)
            return PreflightSettingsManager::instance();
        });

    // FlightSession and OperatorManager are C++ singletons created in the plugin
    // constructor; register them so QML can reference them via com.uav.preflight.
    qmlRegisterSingletonInstance("com.uav.preflight", 1, 0, "FlightSession", FlightSession::instance());
    qmlRegisterSingletonInstance("com.uav.preflight", 1, 0, "OperatorManager", OperatorManager::instance());

    // Expose C++ objects as QML context properties — available globally in QML
    // without needing an import. Each lets QML bind directly to the manager's
    // properties and invoke its methods.
    if (_weatherProvider) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("WeatherProvider"), _weatherProvider);
    }
    if (_telemetryBridge) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("TelemetryProvider"), _telemetryBridge);
    }
    if (_preflightManager) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("PreflightManager"), _preflightManager);
    }
    if (_checklistModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("PreflightChecklistModel"), _checklistModel);
    }
    if (_armingGate) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ArmingGate"), _armingGate);
    }
    if (_vehicleProfileManager) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("VehicleProfileManager"), _vehicleProfileManager);
    }
    if (_powerModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("PowerModel"), _powerModel);
    }
    if (_exportHelper) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ExportHelper"), _exportHelper);
    }
    if (_hardwareTestController) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("HardwareTestController"), _hardwareTestController);
    }
    if (_controlSurfaceTestController) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ControlSurfaceTestController"), _controlSurfaceTestController);
    }
    if (_checklistItemModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("PreflightModel"), _checklistItemModel);
    }
    if (_checklistEngine) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ChecklistEngine"), _checklistEngine);
    }
    qmlEngine->rootContext()->setContextProperty(QStringLiteral("VehicleRegistry"), VehicleRegistry::instance());
    qmlEngine->rootContext()->setContextProperty(QStringLiteral("Database"), &DatabaseManager::instance());
    qmlRegisterType<FlightHistoryModel>("com.uav.preflight", 1, 0, "FlightHistoryModel");
    // Register the NoFlyZoneModel type (properties/enums only; the live instance
    // is provided as the NoFlyZoneModel context property). QML cannot resolve
    // Q_ENUM constants through a context-property instance, so the page uses
    // NoFlyZoneModelRoles.<Role> for role IDs (see AirspacePage.qml).
    qmlRegisterUncreatableType<NoFlyZoneModel>("com.uav.preflight", 1, 0, "NoFlyZoneModelRoles",
                                               QStringLiteral("NoFlyZoneModelRoles provides NoFlyZoneModel role constants only"));
    // Register motor-test state enums for QML. QML cannot resolve Q_ENUM
    // constants through the HardwareTestController context-property instance,
    // so MotorCheckPanel.qml references HardwareTestControllerRoles.<State>
    // (Idle/Testing/Pass/Fail/Cooldown), mirroring the NoFlyZoneModelRoles pattern.
    qmlRegisterUncreatableType<HardwareTestController>("com.uav.preflight", 1, 0, "HardwareTestControllerRoles",
                                                       QStringLiteral("HardwareTestControllerRoles provides HardwareTestController motor-state constants only"));
    if (_noFlyZoneModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("NoFlyZoneModel"), _noFlyZoneModel);
    }
    if (_vehicleListModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("VehicleListModel"), _vehicleListModel);
    }

    // Register per-category filter models as CatModel0..CatModel7 so QML
    // category sections can each bind to their own filtered checklist view.
    for (int i = 0; i < 8; ++i) {
        QString name = QStringLiteral("CatModel%1").arg(i);
        qmlEngine->rootContext()->setContextProperty(name, _categoryModels[i]);
    }

    qCDebug(preflightPluginLog) << "PreflightPlugin: QML context properties and singletons registered";
    return qmlEngine;
}

// ---------------------------------------------------------------------------
// createRootWindow() — Loads the main window QML, then resolves the Plan
// View's MissionController so the zone-compliance check can read the planned
// route.  MainWindow.qml instantiates PlanView (with its PlanMasterController)
// eagerly but hidden, so the controller exists by the time the load completes.
// ---------------------------------------------------------------------------
void PreflightPlugin::createRootWindow(QQmlApplicationEngine *qmlEngine)
{
    QGCCorePlugin::createRootWindow(qmlEngine);
    _resolvePlanMissionController(qmlEngine);
}

void PreflightPlugin::_resolvePlanMissionController(QQmlApplicationEngine *qmlEngine)
{
    QList<PlanMasterController *> found;
    const auto roots = qmlEngine->rootObjects();
    for (QObject *root : roots) {
        found.append(root->findChildren<PlanMasterController *>());
    }

    // Prefer the Plan View's controller (flyView == false) — that is the
    // mission the operator edits.  Fall back to the Fly View's copy.
    PlanMasterController *planController = nullptr;
    PlanMasterController *flyController = nullptr;
    for (PlanMasterController *pmc : found) {
        if (pmc->property("flyView").toBool()) {
            if (!flyController)
                flyController = pmc;
        } else if (!planController) {
            planController = pmc;
        }
    }

    PlanMasterController *chosen = planController ? planController : flyController;
    if (!chosen) {
        qCWarning(preflightPluginLog) << "PreflightPlugin: no PlanMasterController found — zone compliance check will report no mission";
        return;
    }

    _planMissionController = chosen->missionController();
    if (auto *zoneCheck = qobject_cast<ZoneComplianceCheck *>(
            _preflightManager
                ? _preflightManager->checkById(QStringLiteral("airspace.zone_compliance"))
                : nullptr)) {
        zoneCheck->setMissionController(_planMissionController);
        qCDebug(preflightPluginLog) << "PreflightPlugin: zone compliance check wired to"
                                    << (planController ? QStringLiteral("Plan View") : QStringLiteral("Fly View"))
                                    << "mission controller";
    }
}

void PreflightPlugin::_wireZoneComplianceCheck()
{
    if (!_preflightManager || !_noFlyZoneModel)
        return;

    auto *zoneCheck = qobject_cast<ZoneComplianceCheck *>(
        _preflightManager->checkById(QStringLiteral("airspace.zone_compliance")));
    if (!zoneCheck)
        return;

    zoneCheck->setZoneModel(_noFlyZoneModel);
    // Re-evaluate whenever the zone set changes (CRUD, activate/deactivate).
    connect(_noFlyZoneModel, &NoFlyZoneModel::zonesChanged,
            zoneCheck, &ZoneComplianceCheck::evaluate);
}

// ---------------------------------------------------------------------------
// _populateChecklistModel() — Converts PreflightManager's AbstractCheck list
// into a flat JSON array and loads it into ChecklistItemModel. This gives
// ChecklistEngine the items it needs to evaluate against telemetry data.
// ---------------------------------------------------------------------------
void PreflightPlugin::_populateChecklistModel()
{
    if (!_checklistItemModel || !_preflightManager)
        return;
    QJsonArray items;
    const auto checks = _preflightManager->checks();
    // Serialize each check to a JSON object with id, label, type, and category
    // so ChecklistItemModel can index them uniformly.
    for (const auto *check : checks) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = check->id();
        obj[QStringLiteral("label")] = check->label();
        obj[QStringLiteral("isManual")] = (check->checkType() == CheckType::Manual);
        obj[QStringLiteral("category")] = check->categoryInt();
        items.append(obj);
    }
    _checklistItemModel->loadFromJson(items);
}

// ---------------------------------------------------------------------------
// _onActiveVehicleChanged() — Called when the user switches between vehicles
// (or connects/disconnects). Sets up telemetry and checks for the new vehicle,
// or tears everything down if no vehicle is active.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onActiveVehicleChanged(Vehicle *vehicle)
{
    qCDebug(preflightPluginLog) << "PreflightPlugin: Active vehicle changed"
                                << (vehicle ? vehicle->id() : -1);

    if (_armStateTracker)
        _armStateTracker->setVehicle(vehicle);
    _trainer.reset();   // the first value on the new link is the instructor position
    emit trainerChanged();
    // Link loss keeps the flight open; vehicle removal closes
    // the connection (and a flight still open, as link_lost).
    QObject::disconnect(_linkLostConn);
    if (_sessionRecorder) {
        if (!vehicle) {
            _sessionRecorder->connectionClosed(QStringLiteral("vehicle_removed"));
        } else if (VehicleLinkManager *links = vehicle->vehicleLinkManager()) {
            QPointer<Vehicle> v(vehicle);
            // QGC reports the link back on the first message it receives, but
            // the vehicle's armed flag only changes on the next HEARTBEAT (1 Hz).
            // Asking "armed?" at once would say armed for a vehicle that landed
            // and disarmed during the outage, and that flight would then close
            // as an ordinary disarm at the moment the link returned. So the
            // restore is handled kLinkRestoreSettleMs later: a disarm heartbeat
            // in between closes the flight as link_lost at the moment of loss,
            // and if the link drops again first, nothing is restored.
            QPointer<VehicleLinkManager> lm(links);
            _linkLostConn = connect(links, &VehicleLinkManager::communicationLostChanged, _sessionRecorder,
                                    [this, v, lm](bool lost) {
                if (lost) {
                    _sessionRecorder->linkLost();
                    return;
                }
                constexpr int kLinkRestoreSettleMs = 2500;
                QTimer::singleShot(kLinkRestoreSettleMs, _sessionRecorder, [this, v, lm]() {
                    if (lm && !lm->communicationLost())
                        _sessionRecorder->linkRestored(v && v->armed());
                });
            });
        }
    }
    // A new connection usually means a new pack (the FC was power-cycled for
    // the swap), so the operator selects the pack again.
    if (_vehicleProfileManager && !_vehicleProfileManager->currentBatterySerial().isEmpty())
        _vehicleProfileManager->setBatterySerial(QString());
    if (vehicle) {
        _setupForVehicle(vehicle);
    } else {
        _telemetryBridge->setVehicle(nullptr);
        // The bench-test controllers must not keep the departing vehicle.
        if (_hardwareTestController)
            _hardwareTestController->setVehicle(nullptr);
        if (_controlSurfaceTestController)
            _controlSurfaceTestController->setVehicle(nullptr);
        _weatherRefreshTimer.stop();
        if (_checklistEngine)
            _checklistEngine->stop();
    }
}

// ---------------------------------------------------------------------------
// _onKnownVehicleConnected() — A previously-configured vehicle reconnected.
// Loads its saved per-vehicle check configuration from the database and
// applies it to override default check thresholds.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onKnownVehicleConnected(int vehicleId)
{
    Q_UNUSED(vehicleId)
    if (!_preflightManager) return;

    QString fingerprint = VehicleRegistry::instance()->currentFingerprint();
    if (fingerprint.isEmpty()) return;

    QString configJson = DatabaseManager::instance().loadVehicleConfig(fingerprint);
    if (configJson.isEmpty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject config = doc.object();
    // Iterate the saved config object; keys are check IDs, values are per-check config.
    // Apply each one so the check uses vehicle-specific thresholds.
    for (auto it = config.begin(); it != config.end(); ++it) {
        AbstractCheck *check = _preflightManager->checkById(it.key());
        if (check) {
            QJsonObject checkConfig = it.value().toObject();
            check->applyVehicleConfig(checkConfig);
        }
    }
    qCDebug(preflightPluginLog) << "Vehicle config loaded for fingerprint" << fingerprint.left(16);
}

// The flight controller's mAh-consumed counter for the first battery, or -1
// when the vehicle has no battery or has not reported one yet.
double PreflightPlugin::_batteryMahConsumed() const
{
    Vehicle *vehicle = _telemetryBridge ? _telemetryBridge->vehicle() : nullptr;
    QmlObjectListModel *batteries = vehicle ? vehicle->batteries() : nullptr;
    auto *battery = (batteries && batteries->count() > 0) ? batteries->value<VehicleBatteryFactGroup *>(0) : nullptr;
    if (!battery)
        return -1;
    const QVariant mah = battery->mahConsumed()->rawValue();
    return mah.isValid() ? mah.toDouble() : -1;
}

static QString sessionModeName()
{
    FlightSession *fs = FlightSession::instance();
    if (!fs) return QString();
    if (fs->isFlight()) return QStringLiteral("Flight");
    if (fs->isTesting()) return QStringLiteral("Testing");
    if (fs->isTraining()) return QStringLiteral("Training");
    return QStringLiteral("None");
}

// _onGateOpened() opens the connection itself, and _openConnectionSession()
// may open a flight through _onGateOpened(); these flags stop the two calling
// each other in a loop and tell _onGateOpened() which kind of start it is.
static bool s_openingFlight = false;
static bool s_joiningArmedFlight = false;

// Opens the connection session once the vehicle's UID (the vehicles-row key)
// is known. A different UID closes the previous connection first.
//
// If the vehicle is already armed at that point and no flight is open, the
// flight is opened here: either the GCS was started (or restarted after a
// crash) while the vehicle flew, or the arm came before the UID was known.
// No further arm transition will come in either case.
void PreflightPlugin::_openConnectionSession()
{
    if (!_sessionRecorder || !_telemetryBridge || !_telemetryBridge->vehicle())
        return;
    QString key = _vehicleProfileManager ? _vehicleProfileManager->currentDeviceUid() : QString();
    if (key.isEmpty())
        key = VehicleRegistry::instance()->currentFingerprint();
    OperatorManager *ops = OperatorManager::instance();
    _sessionRecorder->connectionOpened(key, sessionModeName(), ops ? ops->currentOperatorName() : QString(),
                                       QCoreApplication::applicationVersion());
    if (!s_openingFlight && _telemetryBridge->vehicle()->armed() && !_sessionRecorder->flightOpen()
        && _sessionRecorder->connectionId() > 0) {
        qCInfo(preflightPluginLog) << "Vehicle already armed when its UID became known: joining the flight";
        s_joiningArmedFlight = true;
        _onGateOpened();
        s_joiningArmedFlight = false;
    }
}

// ---------------------------------------------------------------------------
// _onGateOpened() — ArmStateTracker saw the vehicle ARM (HEARTBEAT), whoever
// armed it. Builds the FlightStart (pack, operator, arm source, session mode,
// bench flag, weather) and hands it to SessionRecorder, which opens the
// flight row. Also called from _openConnectionSession() to join a flight
// that was already armed; s_joiningArmedFlight tells the two apart.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onGateOpened()
{
    if (!_sessionRecorder)
        return;
    s_openingFlight = true;
    _openConnectionSession();   // no-op when already open for this vehicle
    s_openingFlight = false;

    SessionRecorder::FlightStart start;
    start.batterySerial = _vehicleProfileManager ? _vehicleProfileManager->currentBatterySerial() : QString();
    start.payloadKg = _vehicleProfileManager ? _vehicleProfileManager->currentPayloadWeightKg() : 0.0;
    start.pack = DatabaseManager::instance().batteryPack(start.batterySerial);
    OperatorManager *ops = OperatorManager::instance();
    start.operatorId = ops ? ops->currentOperatorId() : -1;
    start.vehicleSysId = _telemetryBridge && _telemetryBridge->vehicle() ? _telemetryBridge->vehicle()->id() : 0;
    if (_weatherProvider && !_weatherProvider->weatherSource().isEmpty())
        start.weatherSummary = QStringLiteral("%1 (%2)").arg(_weatherProvider->currentSummary(),
                                                            _weatherProvider->weatherSource());
    const auto source = _armStateTracker ? _armStateTracker->lastArmSource() : ArmStateTracker::ArmSource::External;
    start.armSource = source == ArmStateTracker::ArmSource::Gcs ? QStringLiteral("gcs")
                    : source == ArmStateTracker::ArmSource::ForceToken ? QStringLiteral("force_token")
                    : QStringLiteral("external");
    start.gateAllowed = _armingGate && _armingGate->isArmingAllowed();
    start.mode = sessionModeName();
    // Bench: Testing mode, a bench force-arm, or a motor test in progress.
    start.bench = start.mode == QLatin1String("Testing") || source == ArmStateTracker::ArmSource::ForceToken
               || (_hardwareTestController && _hardwareTestController->isRunning());
    if (s_joiningArmedFlight) {
        // Who armed is unknown, so no force-arm bench flag from a stale source.
        start.bench = start.mode == QLatin1String("Testing");
        _sessionRecorder->joinArmedFlight(start);
    } else {
        _sessionRecorder->armed(start);
    }

    const int flightId = _sessionRecorder->flightId();
    if (flightId > 0) {
        _vehicleProfileManager->setFlightSessionId(flightId);
        // The target set on the gimbal page during preflight belongs to this
        // flight, and to its Flight History row.
        DatabaseManager &db = DatabaseManager::instance();
        db.copyVehicleTargetToSession(flightId, _sessionRecorder->deviceUid());
        const QVariantMap target = db.vehicleTarget(_sessionRecorder->deviceUid());
        if (!target.isEmpty() && _sessionRecorder->historyFlightId() > 0)
            db.saveTargetLocation(_sessionRecorder->historyFlightId(), target.value(QStringLiteral("lat")).toDouble(),
                                  target.value(QStringLiteral("lon")).toDouble(),
                                  target.value(QStringLiteral("source")).toString());
    }
}

// The chip's "SH high = instructor" box. Saved as "1"/"0" in check config,
// read back at start-up by trainerHighIsInstructorFromSetting().
void PreflightPlugin::setTrainerHighIsInstructor(bool v)
{
    if (v == _trainer.highIsInstructor())
        return;
    _trainer.setHighIsInstructor(v);
    DatabaseManager::instance().setCheckConfig(QStringLiteral("trainer"), QStringLiteral("high_is_instructor"),
                                               v ? QStringLiteral("1") : QStringLiteral("0"));
    emit trainerChanged();
}

int PreflightPlugin::_flightModeChannel() const
{
    if (!_telemetryBridge || !_telemetryBridge->hasParameter(QStringLiteral("FLTMODE_CH")))
        return -1;
    return qRound(_telemetryBridge->parameterValue(QStringLiteral("FLTMODE_CH"), 0.0f));
}

// "Detect": 15 s in which the operator flicks SH; the channel that went
// clearly low and high a few times (not a stick, not the mode channel) wins.
void PreflightPlugin::startTrainerDetect()
{
    _detector.start(_flightModeChannel());
    _detectRunning = true;
    _detectSecsLeft = 15;
    _detectFound = -1;
    _detectStatus = tr("Flick SH down and up 2 times\u2026");
    _detectTimer.start();
    emit trainerDetectChanged();
}

void PreflightPlugin::cancelTrainerDetect()
{
    _detectTimer.stop();
    _detectRunning = false;
    _detectFound = -1;
    _detectStatus.clear();
    emit trainerDetectChanged();
}

void PreflightPlugin::acceptTrainerDetect()
{
    if (_detectFound > 0)
        setTrainerChannel(_detectFound);
    cancelTrainerDetect();
}

void PreflightPlugin::_finishTrainerDetect()
{
    _detectTimer.stop();
    _detectRunning = false;
    int channel = -1;
    QList<int> several;
    switch (_detector.finish(&channel, &several)) {
    case TrainerChannelDetector::Outcome::Found:
        _detectFound = channel;
        _detectStatus = tr("Found: CH%1").arg(channel);
        break;
    case TrainerChannelDetector::Outcome::Several: {
        QStringList names;
        for (int c : several)
            names << QStringLiteral("CH%1").arg(c);
        _detectStatus = tr("Several channels moved (%1) \u2014 flick only SH and try again").arg(names.join(QStringLiteral(", ")));
        break;
    }
    case TrainerChannelDetector::Outcome::OnlyModeChannel:
        _detectStatus = tr("Only the flight-mode channel moved \u2014 SH must be on another channel");
        break;
    case TrainerChannelDetector::Outcome::NoRc:
        _detectStatus = tr("No RC data from the plane \u2014 is the RC connected?");
        break;
    case TrainerChannelDetector::Outcome::NoMovement:
    case TrainerChannelDetector::Outcome::Running:
        _detectStatus = tr("No switch seen \u2014 is the RC connected and is SH sent to the plane?");
        break;
    }
    emit trainerDetectChanged();
}

QString PreflightPlugin::trainerChannelConflict() const
{
    const int modeCh = _flightModeChannel();
    if (modeCh < 0)
        return {};
    return modeCh == _trainer.channel()
        ? tr("CH%1 is the flight-mode channel (FLTMODE_CH) \u2014 mix the trainer switch onto another channel")
              .arg(modeCh)
        : QString();
}

void PreflightPlugin::setTrainerChannel(int channel)
{
    if (channel < 1 || channel > 18 || channel == _trainer.channel())
        return;
    _trainer.setChannel(channel);
    DatabaseManager::instance().setCheckConfig(QStringLiteral("trainer"), QStringLiteral("channel"),
                                               QString::number(channel));
    _onRcChannels();
    emit trainerChanged();
}

// Who has control in two-radio training. A real switch (instructor <->
// student) is logged as a handover in Training sessions only: in other modes
// the same channel may be used for something else.
void PreflightPlugin::_onRcChannels()
{
    if (!_telemetryBridge)
        return;
    if (_detectRunning) {
        _detector.feed(_telemetryBridge->rcChannelValues());
        if (_detector.clearEarly())
            _finishTrainerDetect();
    }
    const TrainerSwitchTracker::Control before = _trainer.control();
    const bool wasVerified = _trainer.verified();
    // The flight-mode switch is not a trainer switch (ArduPlane's FLTMODE_CH
    // defaults to 8): never read mode changes as handovers.
    if (trainerChannelConflict().length() > 0) {
        if (_trainer.update(QVariantList()))      // -> unmonitored
            emit trainerChanged();
        return;
    }
    if (!_trainer.update(_telemetryBridge->rcChannelValues())) {
        if (wasVerified != _trainer.verified())
            emit trainerChanged();
        return;
    }
    const TrainerSwitchTracker::Control now = _trainer.control();
    const bool handover = before != TrainerSwitchTracker::Control::Unmonitored
                       && now != TrainerSwitchTracker::Control::Unmonitored;
    FlightSession *fs = FlightSession::instance();
    if (handover && fs && fs->isTraining() && _sessionRecorder) {
        _sessionRecorder->trainerHandover(now == TrainerSwitchTracker::Control::Student,
                                          QStringLiteral("trainer switch, CH%1").arg(_trainer.channel()));
        // A short toast on the Fly view rather than an app message: a
        // handover happens many times a lesson and must not need dismissing.
        emit trainerHandoverAnnounced(now == TrainerSwitchTracker::Control::Student
                                          ? tr("STUDENT has control")
                                          : tr("INSTRUCTOR has control"),
                                      now == TrainerSwitchTracker::Control::Student);
    }
    emit trainerChanged();
}

// ---------------------------------------------------------------------------
// _onGateClosed() — ArmStateTracker saw the vehicle DISARM. SessionRecorder
// closes the flight; its closed-handler does the battery cycle and calibration.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onGateClosed(const QString &reason)
{
    Q_UNUSED(reason)
    if (_sessionRecorder)
        _sessionRecorder->disarmed();
}

// ---------------------------------------------------------------------------
// _setupForVehicle() — Wires all managers to a specific vehicle. Called both
// on initial connection and when switching active vehicles. Starts telemetry
// streaming, checklist evaluation, and weather refresh.
// ---------------------------------------------------------------------------
void PreflightPlugin::_setupForVehicle(Vehicle *vehicle)
{
    if (!vehicle || !_telemetryBridge || !_preflightManager) return;

    _telemetryBridge->setVehicle(vehicle);
    if (_hardwareTestController) {
        _hardwareTestController->setVehicle(vehicle);
    }
    if (_controlSurfaceTestController) {
        _controlSurfaceTestController->setVehicle(vehicle);
    }
    FlightSession::instance()->setVehicle(vehicle);
    _preflightManager->startEvaluation(1000);
    if (_checklistEngine)
        _checklistEngine->start();

    if (_weatherProvider) {
        auto *settings = PreflightSettingsManager::instance();
        if (settings && settings->autoWeatherEnabled()) {
            _refreshWeather();
            _startWeatherRefresh();
        }
    }

    qCDebug(preflightPluginLog) << "PreflightPlugin: TelemetryBridge connected to vehicle"
                                << vehicle->id();
}

// ---------------------------------------------------------------------------
// _refreshWeather() — Fetches weather data using either a configured ICAO code
// or the vehicle's current GPS position as a fallback.
// ---------------------------------------------------------------------------
void PreflightPlugin::_refreshWeather()
{
    if (!_weatherProvider || !_telemetryBridge) return;

    auto *settings = PreflightSettingsManager::instance();
    if (!settings || !settings->autoWeatherEnabled()) return;

    QString icao = settings->defaultIcao();
    if (!icao.isEmpty()) {
        // Prefer manual ICAO station override
        _weatherProvider->refreshAll(icao);
    } else if (_telemetryBridge->vehicle()) {
        // Fall back to vehicle GPS coordinates
        double lat = _telemetryBridge->vehicle()->latitude();
        double lon = _telemetryBridge->vehicle()->longitude();
        if (qAbs(lat) > 0.01 || qAbs(lon) > 0.01)
            _weatherProvider->fetchWeather(lat, lon);
    }
}

// Starts the periodic weather refresh timer using the user-configured interval.
void PreflightPlugin::_startWeatherRefresh()
{
    auto *settings = PreflightSettingsManager::instance();
    int interval = settings ? settings->weatherUpdateIntervalMin() : 5;
    _weatherRefreshTimer.start(interval * 60 * 1000);
}

// ---------------------------------------------------------------------------
// analyzePages() — Returns the list of pages shown in the Analyze tab.
// Lazily built: copies the base QGC pages then appends custom ones.
// ---------------------------------------------------------------------------
const QVariantList &PreflightPlugin::analyzePages()
{
    if (_analyzePages.isEmpty()) {
        const QVariantList &basePages = QGCCorePlugin::analyzePages();
        for (const auto &page : basePages) {
            _analyzePages.append(page);
        }

#ifdef QT_DEBUG
#endif

        // Append custom plugin pages after the stock QGC pages
        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Preflight Checklist"),
                QUrl(QStringLiteral("qrc:/qml/cpts/PreflightChecklistView.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/qmlimages/check.svg")),
                this)));

        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Vehicles"),
                QUrl(QStringLiteral("qrc:/qml/analyze/VehiclesPage.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/qmlimages/Plan.svg")),
                this)));

        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Flight History"),
                QUrl(QStringLiteral("qrc:/qml/pages/FlightHistoryPage.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/qmlimages/Plan.svg")),
                this)));

        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Airspace"),
                QUrl(QStringLiteral("qrc:/qml/pages/AirspacePage.qml")),
                QUrl::fromUserInput(QStringLiteral("qrc:/custom/icons/airspace.svg")),
                this)));
    }
    return _analyzePages;
}

// ---------------------------------------------------------------------------
// paletteOverride() — Custom theming for the Skywin GCS dark theme.
// Called by QGCPalette for each named color. Overrides the dark theme entries
// with a steel-blue accent on near-black backgrounds. Safety colors (green,
// red, yellow) are intentionally NOT overridden so they remain standard.
// Light theme is left at QGC defaults.
// ---------------------------------------------------------------------------
void PreflightPlugin::paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo)
{
    // ══════════════════════════════════════════════════════════════════════
    //  Skywin GCS Theme — Steel Blue accent (#3B82A0)
    //  Near-black backgrounds, off-white text, single accent throughout.
    //  Safety colors (colorGreen, colorRed, colorYellow) are NOT overridden.
    //  colorOrange replaced with accent for decorative use.
    //  Dark theme only — light theme left at defaults.
    // ══════════════════════════════════════════════════════════════════════

    static const QColor kAccent         ("#3B82A0");   // steel blue — ONE value
    static const QColor kAccentDim      ("#2A5F78");   // darker shade for disabled
    static const QColor kBgWindow       ("#0B0D12");   // near-black with blue tint
    static const QColor kBgShade        ("#080A0F");
    static const QColor kBgShadeDark    ("#060810");
    static const QColor kBgShadeLight   ("#12151C");
    static const QColor kTextEnabled    ("#E0E4EC");   // slightly off-white
    static const QColor kTextDisabled   ("#5A6272");
    static const QColor kTextMuted      ("#6B7A8F");   // muted blue-grey for idle icons
    static const QColor kBtnBg          ("#0E1018");
    static const QColor kBtnBorder      ("#1A1E28");
    static const QColor kBtnBorderAct   ("#3B82A0");

    // ── Backgrounds ────────────────────────────────────────────────────────
    if (colorName == QStringLiteral("window")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgWindow;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgWindow;
    } else if (colorName == QStringLiteral("windowShade")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShade;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgShade;
    } else if (colorName == QStringLiteral("windowShadeDark")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShadeDark;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgShadeDark;
    } else if (colorName == QStringLiteral("windowShadeLight")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShadeLight;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgShadeLight;

    // ── Text ───────────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("text")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kTextEnabled;
    } else if (colorName == QStringLiteral("warningText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;

    // ── Buttons ────────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("button")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBtnBg;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBtnBg;
    } else if (colorName == QStringLiteral("buttonBorder")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBtnBorder;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBtnBorderAct;
    } else if (colorName == QStringLiteral("buttonText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kTextMuted;
    } else if (colorName == QStringLiteral("buttonHighlight")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("buttonHighlightText")) {
        // Text drawn ON TOP of buttonHighlight (e.g. checked SubMenuButton in
        // the Analyze Tools sidebar).  Must contrast with the kAccent button
        // highlight background — using kAccent here rendered the selected nav
        // item invisible (teal-on-teal).  Off-white keeps the label readable.
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#F2F6FA");
    } else if (colorName == QStringLiteral("primaryButton")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("primaryButtonText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextEnabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#000000");

    // ── Text fields ────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("textField")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShadeDark;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgWindow;
    } else if (colorName == QStringLiteral("textFieldText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kTextEnabled;

    // ── Map ────────────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("mapButton")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgWindow;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgWindow;
    } else if (colorName == QStringLiteral("mapButtonHighlight")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("mapIndicator")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("mapIndicatorChild")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;

    // ── Accent colors — replace decorative orange with accent ─────────────
    } else if (colorName == QStringLiteral("colorOrange")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("colorBlue")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("colorGrey")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#4B5563");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#8B95A8");

    // ── Branding — accent-derived ──────────────────────────────────────────
    } else if (colorName == QStringLiteral("brandingPurple")) {
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]   = kAccent;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]   = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]    = kAccent;
    } else if (colorName == QStringLiteral("brandingBlue")) {
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]   = kAccent;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]   = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]    = kAccent;

    // ── Toolbar ────────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("toolbarBackground")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShadeDark;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#090B10");
    } else if (colorName == QStringLiteral("toolStripHoverColor")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgShadeLight;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#1E3040");

    // ── Status text ────────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("statusPassedText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kTextEnabled;
    } else if (colorName == QStringLiteral("statusFailedText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kAccentDim;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
    } else if (colorName == QStringLiteral("statusPendingText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#4B5563");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#8B95A8");

    // ── Mission editor ────────────────────────────────────────────────────
    } else if (colorName == QStringLiteral("missionItemEditor")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBgWindow;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBgShadeLight;
    } else if (colorName == QStringLiteral("groupBorder")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kBtnBorder;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kBtnBorderAct;
    }
}

// Scales down the default font size to fit the plugin's custom layout proportions.
bool PreflightPlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData)
{
    if (settingsGroup == AppSettings::settingsGroup) {
        if (metaData.name() == AppSettings::appFontPointSizeName) {
            QVariant curDefault = metaData.rawDefaultValue();
            double scaledDefault = curDefault.toDouble() * 0.75;
            double minVal = metaData.rawMin().toDouble();
            if (scaledDefault < minVal)
                scaledDefault = minVal;
            metaData.setRawDefaultValue(scaledDefault);
            return true;
        }
    }
    return QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData);
}

// Intercepts incoming MAVLink messages for arm/disarm gating.
// COMMAND_LONG with MAV_CMD_COMPONENT_ARM_DISARM from companion/external GCS
// is blocked when the gate is closed. COMMAND_ACK for arm/disarm is logged.
bool PreflightPlugin::mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message)
{
    Q_UNUSED(link)

    if (message.msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
        mavlink_command_long_t cmd;
        mavlink_msg_command_long_decode(&message, &cmd);

        if (cmd.command == MAV_CMD_COMPONENT_ARM_DISARM) {
            // Single source of truth for arm gating: ArmingGate::interceptCommandLong.
            // All inbound arm commands (force-arm included) are evaluated there; an
            // inbound force-arm (param2 == kForceArmParam2/kForceDisarmParam2) must
            // NOT auto-open the gate — ArmingGate only honors a local operator override
            // or one-time ACK, so any link peer keeps being gated exactly like a normal
            // arm command (see the force-arm note on ArmingGate::forceArm()).
            if (_armingGate) {
                QMap<int, float> params;
                params.insert(1, cmd.param1);
                params.insert(2, cmd.param2);
                if (!_armingGate->interceptCommandLong(cmd.command, params)) {
                    return false;  // Consume the message — vehicle won't see it
                }
            }
        }
    }

    if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
        mavlink_command_ack_t ack;
        mavlink_msg_command_ack_decode(&message, &ack);

        if (ack.command == MAV_CMD_COMPONENT_ARM_DISARM) {
            // An ACK never changes armed state; Vehicle::armedChanged
            // (HEARTBEAT) is the only source (ArmStateTracker). The ACK only labels
            // the arm source and surfaces rejections.
            if (_armStateTracker)
                _armStateTracker->onArmDisarmAck(ack.result == MAV_RESULT_ACCEPTED);
            if (ack.result == MAV_RESULT_ACCEPTED) {
                qCInfo(preflightPluginLog) << "Vehicle arm/disarm command ACCEPTED by autopilot";
            } else {
                QString reason = _mavResultToString(ack.result);
                qCWarning(preflightPluginLog) << QStringLiteral("Vehicle arm DENIED (result=%1: %2)").arg(ack.result).arg(reason);
                _surfaceCommandRejection(QStringLiteral("Arm"), reason);
            }
        }

        // Surface rejections for takeoff and mode-change commands (not just arm).
        if (ack.result != MAV_RESULT_ACCEPTED) {
            switch (ack.command) {
            case MAV_CMD_NAV_TAKEOFF:
                _surfaceCommandRejection(QStringLiteral("Takeoff"), _mavResultToString(ack.result));
                break;
            case MAV_CMD_DO_SET_MODE:
                _surfaceCommandRejection(QStringLiteral("Mode change"), _mavResultToString(ack.result));
                break;
            default:
                break;
            }
        }
    }

    return true;  // Let the message continue through QGC's normal processing
}

// Convert a MAV_RESULT enum to a short human-readable denial reason.
QString PreflightPlugin::_mavResultToString(int result)
{
    switch (result) {
    case MAV_RESULT_TEMPORARILY_REJECTED: return QStringLiteral("Temporarily rejected by autopilot");
    case MAV_RESULT_DENIED:              return QStringLiteral("Denied by autopilot");
    case MAV_RESULT_UNSUPPORTED:         return QStringLiteral("Unsupported command");
    case MAV_RESULT_FAILED:              return QStringLiteral("Command failed");
    case MAV_RESULT_IN_PROGRESS:         return QStringLiteral("In progress");
    case MAV_RESULT_CANCELLED:           return QStringLiteral("Cancelled");
    default:                             return QStringLiteral("Unknown error (result=%1)").arg(result);
    }
}

// Surface a command rejection to the user via signal + property.
// Stores the last rejection so QML can bind to it, and emits a transient signal
// for toast/banner display.
void PreflightPlugin::_surfaceCommandRejection(const QString &command, const QString &reason)
{
    QString message = QStringLiteral("%1: %2").arg(command, reason);
    qCWarning(preflightPluginLog) << QStringLiteral("FC rejection surfaced: %1").arg(message);
    if (_armingGate) {
        emit _armingGate->commandRejected(message);
    }
}

// Returns the list of custom toolbar indicators (shown in the top toolbar).
const QVariantList &PreflightPlugin::toolBarIndicators()
{
    if (_toolBarIndicators.isEmpty()) {
        // Plugin indicators are the supported way into the Fly-view toolbar:
        // the stock FlyViewToolBar is AOT-compiled into QGroundControl.Controls,
        // so a custom.qrc copy of it never loads.
        _toolBarIndicators = QVariantList({
            QVariant::fromValue(QUrl("qrc:/qml/cpts/SessionStatusChip.qml")),
            QVariant::fromValue(QUrl("qrc:/custom/PreflightToolbarIndicator.qml")),
        });
    }
    return _toolBarIndicators;
}

// ---------------------------------------------------------------------------
// ArmStateTracker — see PreflightPlugin.h.
// ---------------------------------------------------------------------------
ArmStateTracker::ArmStateTracker(ArmingGate *gate, QObject *parent)
    : QObject(parent), m_gate(gate)
{
}

void ArmStateTracker::setVehicle(Vehicle *vehicle)
{
    if (m_vehicle == vehicle)
        return;
    QObject::disconnect(m_armedConn);
    m_vehicle = vehicle;
    m_armed = vehicle ? vehicle->armed() : false;
    m_lastGcsAckMs = -1;
    if (vehicle)
        m_armedConn = connect(vehicle, &Vehicle::armedChanged, this, &ArmStateTracker::onArmedChanged);
}

ArmStateTracker::ArmSource ArmStateTracker::sourceFor(qint64 gcsAckAgeMs, bool forceWindowOpen)
{
    if (forceWindowOpen)
        return ArmSource::ForceToken;
    if (gcsAckAgeMs >= 0 && gcsAckAgeMs <= kGcsAckWindowMs)
        return ArmSource::Gcs;
    return ArmSource::External;
}

void ArmStateTracker::onArmDisarmAck(bool accepted)
{
    if (!accepted)
        return;
    if (!m_armed) {
        // Disarmed: an accepted ARM_DISARM is (almost certainly) this GCS's arm.
        m_lastGcsAckMs = MonotonicClock::nowMs();
        return;
    }
    // Already armed: the HEARTBEAT beat the ACK. If the arm was just labelled
    // External, this late ACK means it was ours.
    if (m_lastArmSource == ArmSource::External && m_armedAtMs >= 0
        && MonotonicClock::ageMs(m_armedAtMs) <= kGcsAckWindowMs) {
        m_lastArmSource = ArmSource::Gcs;
        qCInfo(preflightPluginLog) << "Arm source corrected to GCS (ACK after HEARTBEAT)";
    }
}

void ArmStateTracker::onArmedChanged(bool armed)
{
    if (armed == m_armed)
        return;   // duplicate notification
    m_armed = armed;
    if (armed) {
        const bool forceWindow = m_gate && m_gate->isOverrideActive();
        m_lastArmSource = sourceFor(MonotonicClock::ageMs(m_lastGcsAckMs), forceWindow);
        m_armedAtMs = MonotonicClock::nowMs();
        m_lastGcsAckMs = -1;
        qCInfo(preflightPluginLog) << "Vehicle ARMED (heartbeat), source:" << m_lastArmSource;
        // TODO(step3): SessionRecorder stores the arm source with the flight.
        if (m_gate) {
            emit m_gate->vehicleArmed();
            emit m_gate->gateOpened();
        }
    } else {
        qCInfo(preflightPluginLog) << "Vehicle DISARMED (heartbeat)";
        if (m_gate) {
            emit m_gate->vehicleDisarmed();
            emit m_gate->gateClosed(QStringLiteral("Vehicle disarmed"));
        }
    }
}
