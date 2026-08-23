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
#include <QRegularExpression>
#include <QtCore/QApplicationStatic>

#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QmlComponentInfo.h"
#include "Vehicle/Vehicle.h"
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

    connect(FlightSession::instance(), &FlightSession::postFlightChecklistRequired,
            _preflightManager, &PreflightManager::activatePostFlightChecks);

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

    // The NoFlyZoneModel constructor reloads zones from the database, so the
    // DB must be initialized before the model is created or the dropdown will
    // always be empty.
    DatabaseManager::instance().initialize();

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

    // --- Wire up the ArmingGate so it can query checks and telemetry ---
    _armingGate = new ArmingGate(this);
    _armingGate->setPreflightManager(_preflightManager);
    _armingGate->setTelemetryBridge(_telemetryBridge);

    // TelemetryBridge needs the gate reference so arm() checks gate state before sending.
    _telemetryBridge->setArmingGate(_armingGate);

    _preflightManager->setTelemetryBridge(_telemetryBridge);

    // Keep blocking rules in sync with the resolved vehicle kind (quad vs
    // fixed wing). Fires on connect and whenever the type re-resolves.
    connect(_vehicleProfileManager, &VehicleProfileManager::vehicleTypeResolved,
            _preflightManager, [this]() {
        if (_vehicleProfileManager && _preflightManager) {
            _preflightManager->applyVehicleKind(_vehicleProfileManager->vehicleKindString());
        }
    });

    // Flatten all checks into ChecklistItemModel and feed to ChecklistEngine
    // which evaluates them against live telemetry each cycle.
    _checklistItemModel = new ChecklistItemModel(this);
    _populateChecklistModel();
    _checklistEngine = new ChecklistEngine(this);
    _checklistEngine->setModel(_checklistItemModel);
    _checklistEngine->setTelemetryBridge(_telemetryBridge);

    // When we get a battery voltage reading and no battery serial has been
    // assigned yet, create a synthetic serial ID from the vehicle ID so
    // the power model can track per-battery cycle counts.
    connect(_telemetryBridge, &TelemetryBridge::batteryVoltageChanged, this, [this]() {
        if (!_telemetryBridge || !_vehicleProfileManager) return;
        if (!_vehicleProfileManager->currentBatterySerial().isEmpty()) return;
        QString sysId = QStringLiteral("batt:%1").arg(_telemetryBridge->vehicle() ? _telemetryBridge->vehicle()->id() : 0);
        _vehicleProfileManager->setBatterySerial(sysId);
    });

    // --- Vehicle registry signals ---
    // knownVehicleConnected: a previously-seen vehicle reconnected; load its saved config.
    // newVehicleRegistered:  a never-seen vehicle appeared; save its initial config (compass, etc.)
    auto *registry = VehicleRegistry::instance();
    connect(registry, &VehicleRegistry::knownVehicleConnected,
            this, &PreflightPlugin::_onKnownVehicleConnected);
    connect(registry, &VehicleRegistry::newVehicleRegistered,
            this, &PreflightPlugin::_onNewVehicleRegistered);

    // Gate open/close tracks the flight session lifecycle for compliance logging.
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

    if (vehicle) {
        _setupForVehicle(vehicle);
    } else {
        _telemetryBridge->setVehicle(nullptr);
        _weatherRefreshTimer.stop();
        if (_checklistEngine)
            _checklistEngine->stop();
    }
}

// ---------------------------------------------------------------------------
// _onKnownVehicleConnected() — A previously-configured vehicle reconnected.
// Loads its saved per-vehicle check configuration from the database and
// applies it to override default check thresholds (e.g., compass orientation).
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

// ---------------------------------------------------------------------------
// _onNewVehicleRegistered() — First-time vehicle detected. Records its initial
// configuration (currently just compass orientation) to the database so future
// reconnections can detect config drift.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onNewVehicleRegistered(int vehicleId)
{
    Q_UNUSED(vehicleId)
    qCDebug(preflightPluginLog) << "New vehicle registered:" << vehicleId;

    // Save initial vehicle config with known compass orientation
    QString fingerprint = VehicleRegistry::instance()->currentFingerprint();
    if (fingerprint.isEmpty()) return;

    AbstractCheck *compassCheck = _preflightManager
        ? _preflightManager->checkById(QStringLiteral("nav.compass.orientation"))
        : nullptr;
    if (!compassCheck) return;

    QJsonObject config;
    QJsonObject compassConfig;

    // Determine the current actual orientation from the check's current value
    // Format: "CAL_MAG0_ROT=Yaw180 (4)" → extract rotation value (4)
    QString cv = compassCheck->getCurrentValueString();
    QRegularExpression re(QStringLiteral("\\((\\d+)\\)$"));
    auto match = re.match(cv);
    if (match.hasMatch()) {
        int rot = match.captured(1).toInt();
        if (rot > 0) {
            compassConfig[QStringLiteral("expectedRotation")] = rot;
            config[QStringLiteral("nav.compass.orientation")] = compassConfig;
            DatabaseManager::instance().saveVehicleConfig(
                fingerprint,
                QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
            qCDebug(preflightPluginLog)
                << "Saved initial compass config for" << fingerprint.left(16)
                << "expectedRotation =" << rot;
        }
    }
}

// ---------------------------------------------------------------------------
// _onGateOpened() — The arming gate just opened (all preflight checks passed
// and the pilot is cleared to arm). Starts a new flight session in the database
// for compliance tracking.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onGateOpened()
{
    if (_currentSessionId >= 0) return;
    QString fp = VehicleRegistry::instance()->currentFingerprint();
    if (fp.isEmpty()) return;
    _currentSessionId = DatabaseManager::instance().startFlightSession(fp, QString());
    _sessionStartTime = QDateTime::currentDateTime();
    qCDebug(preflightPluginLog) << "Flight session started:" << _currentSessionId;
}

// ---------------------------------------------------------------------------
// _onGateClosed() — The arming gate closed (disarmed or check failure).
// Ends the flight session: calculates duration, writes a compliance log entry
// with session metadata, and increments the battery cycle counter.
// ---------------------------------------------------------------------------
void PreflightPlugin::_onGateClosed(const QString &reason)
{
    Q_UNUSED(reason)
    if (_currentSessionId < 0) return;
    double duration = _sessionStartTime.isValid()
        ? _sessionStartTime.secsTo(QDateTime::currentDateTime())
        : 0.0;

    // Save compliance log for this session
    QString fp = VehicleRegistry::instance()->currentFingerprint();
    if (!fp.isEmpty()) {
        QJsonObject logEntry;
        logEntry[QStringLiteral("sessionId")] = _currentSessionId;
        logEntry[QStringLiteral("deviceUid")] = fp;
        logEntry[QStringLiteral("durationSec")] = duration;
        logEntry[QStringLiteral("startTime")] = _sessionStartTime.toString(Qt::ISODate);
        logEntry[QStringLiteral("endTime")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        logEntry[QStringLiteral("gateCloseReason")] = reason;
        QString logJson = QString::fromUtf8(QJsonDocument(logEntry).toJson(QJsonDocument::Compact));
        DatabaseManager::instance().saveComplianceLog(
            QStringLiteral("session-%1").arg(_currentSessionId),
            fp, QString(), QString(), logJson, QString());
    }

    DatabaseManager::instance().endFlightSession(_currentSessionId, duration);
    DatabaseManager::instance().incrementBatteryCycle(_currentSessionId);
    qCDebug(preflightPluginLog) << "Flight session ended:" << _currentSessionId << "duration:" << duration << "s";
    _currentSessionId = -1;
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
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = kTextDisabled;
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = kAccent;
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
            _lastArmDisarmParam = static_cast<int>(cmd.param1);
            if (cmd.param1 == 1.0f) {
                if (_armingGate && !_armingGate->isArmingAllowed() && !_armingGate->isOverrideActive()) {
                    QString reason = _armingGate->denialReason();
                    qWarning().noquote() << QStringLiteral("ArmingGate: BLOCKED incoming COMMAND_LONG arm from compid %1 — %2")
                        .arg(message.compid).arg(reason);
                    emit _armingGate->armingDenied(cmd.command, reason);
                    return false;  // Consume the message — vehicle won't see it
                }
            }
        }
    }

    if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
        mavlink_command_ack_t ack;
        mavlink_msg_command_ack_decode(&message, &ack);

        if (ack.command == MAV_CMD_COMPONENT_ARM_DISARM) {
            if (ack.result == MAV_RESULT_ACCEPTED) {
                qCWarning(preflightPluginLog) << "Vehicle armed/disarmed successfully";
                if (_armingGate) {
                    if (_lastArmDisarmParam == 1)
                        emit _armingGate->vehicleArmed();
                    else if (_lastArmDisarmParam == 0)
                        emit _armingGate->vehicleDisarmed();
                }
            } else {
                qCWarning(preflightPluginLog) << QStringLiteral("Vehicle arm DENIED (result=%1)").arg(ack.result);
            }
            _lastArmDisarmParam = -1;
        }
    }

    return true;  // Let the message continue through QGC's normal processing
}

// Returns the list of custom toolbar indicators (shown in the top toolbar).
const QVariantList &PreflightPlugin::toolBarIndicators()
{
    if (_toolBarIndicators.isEmpty()) {
        _toolBarIndicators = QVariantList({
            QVariant::fromValue(QUrl("qrc:/custom/PreflightToolbarIndicator.qml")),
        });
    }
    return _toolBarIndicators;
}
