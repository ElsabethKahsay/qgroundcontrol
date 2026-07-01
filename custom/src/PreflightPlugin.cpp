#include "PreflightPlugin.h"

#include <QDebug>
#include <QJSEngine>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QtCore/QApplicationStatic>

#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QmlComponentInfo.h"
#include "Vehicle/Vehicle.h"

Q_DECLARE_METATYPE(Vehicle*)

// Force QML module type registrations from static libraries to be included.
// (Linker discards QQmlModuleRegistration globals from static archives
// unless something references the object.)
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
int qInitResources_qmlcache_FlightDisplayModule();
int qInitResources_qmlcache_FlightMapModule();
int qInitResources_qmlcache_MainWindowModule();
int qInitResources_qmlcache_QGroundControlControlsModule();
int qInitResources_qmlcache_ScreenToolsModule();
int qInitResources_qmlcache_ToolbarModule();
int qInitResources_qmlcache_VehicleSetupModule();

#include "AbstractCheck.h"
#include "adapters/TelemetryBridge.h"
#include "core/ArmingGate.h"
#include "core/BatteryHealthCheck.h"
#include "core/MissionEnergyCheck.h"
#include "core/PowerModel.h"
#include "core/VehicleProfileManager.h"
#include "PreflightChecklistFilterModel.h"
#include "PreflightChecklistModel.h"
#include "PreflightManager.h"
#include "PreflightSettingsManager.h"
#include "controllers/HardwareTestController.h"
#include "utils/Config.h"
#include "utils/DatabaseManager.h"
#include "utils/ExportHelper.h"
#include "utils/WeatherProvider.h"

Q_LOGGING_CATEGORY(preflightPluginLog, "preflight.plugin")

Q_APPLICATION_STATIC(PreflightPlugin, _preflightPluginInstance)

extern int qInitResources_custom();

PreflightPlugin::PreflightPlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
    qInitResources_custom();

    // Force-link all module type registrations and qmlcache objects from static libs
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
    qInitResources_qmlcache_FlightMapModule();
    qInitResources_qmlcache_FlightDisplayModule();
    qInitResources_qmlcache_ToolbarModule();
    qInitResources_qmlcache_VehicleSetupModule();
    qInitResources_qmlcache_FactControlsModule();
    qInitResources_qmlcache_AppSettingsModule();
    qInitResources_qmlcache_QGroundControlControlsModule();
    qInitResources_qmlcache_MainWindowModule();
    qInitResources_qmlcache_ScreenToolsModule();

    // Register Vehicle with QML so Q_PROPERTY(Vehicle* ...) with REQUIRED works
    qmlRegisterUncreatableType<Vehicle>("QGroundControl", 1, 0, "Vehicle", QStringLiteral("Cannot create Vehicle from QML"));

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

void PreflightPlugin::init()
{
    QGCCorePlugin::init();

    _preflightManager = new PreflightManager(1, this);
    _checklistModel = new PreflightChecklistModel(this);
    _checklistModel->setPreflightManager(_preflightManager);
    _telemetryBridge = new TelemetryBridge(this);

    for (int i = 0; i < 8; ++i) {
        _categoryModels[i] = new PreflightChecklistFilterModel(this);
        _categoryModels[i]->setSourceModel(static_cast<QAbstractItemModel *>(_checklistModel));
        _categoryModels[i]->setCategoryId(i);
    }

    _weatherProvider = new WeatherProvider(this);
    connect(&_weatherRefreshTimer, &QTimer::timeout, this, &PreflightPlugin::_refreshWeather);

    _hardwareTestController = new HardwareTestController(this);

    _powerModel = new PowerModel(this);
    _exportHelper = new ExportHelper(this);

    _vehicleProfileManager = new VehicleProfileManager(this);
    _vehicleProfileManager->setTelemetryBridge(_telemetryBridge);

    auto *batteryHealthCheck = new BatteryHealthCheck(
        _vehicleProfileManager, kBatteryHealthVoltageThreshold, kBatteryHealthStddevThreshold, this);
    _preflightManager->addCheck(batteryHealthCheck);

    auto *energyCheck = new MissionEnergyCheck(_powerModel, kMissionEnergyMargin, this);
    energyCheck->setVehicleProfileManager(_vehicleProfileManager);
    _preflightManager->addCheck(energyCheck);

    _armingGate = new ArmingGate(this);
    _armingGate->setPreflightManager(_preflightManager);
    _armingGate->setTelemetryBridge(_telemetryBridge);

    _preflightManager->setTelemetryBridge(_telemetryBridge);

    connect(_telemetryBridge, &TelemetryBridge::batteryVoltageChanged, this, [this]() {
        if (!_telemetryBridge || !_vehicleProfileManager) return;
        if (!_vehicleProfileManager->currentBatterySerial().isEmpty()) return;
        QString sysId = QStringLiteral("batt:%1").arg(_telemetryBridge->vehicle() ? _telemetryBridge->vehicle()->id() : 0);
        _vehicleProfileManager->setBatterySerial(sysId);
    });

    qCDebug(preflightPluginLog) << "PreflightPlugin: init complete,"
                                << _preflightManager->totalChecks() << "checks";

    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
            this, &PreflightPlugin::_onActiveVehicleChanged);

    Vehicle *activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (activeVehicle) {
        _setupForVehicle(activeVehicle);
    }
}

QQmlApplicationEngine *PreflightPlugin::createQmlApplicationEngine(QObject *parent)
{
    QQmlApplicationEngine *qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);

    qmlEngine->addImportPath(QStringLiteral("qrc:/qml/cpts"));

    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/Colors.qml")), "com.uav.preflight", 1, 0, "Colors");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/Config.qml")), "com.uav.preflight", 1, 0, "Config");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/singletons/VehicleTelemetry.qml")), "com.uav.preflight", 1, 0, "VehicleTelemetry");
    qmlRegisterUncreatableType<AbstractCheck>("com.uav.preflight", 1, 0, "AbstractCheck", QStringLiteral("Cannot create AbstractCheck from QML"));
    qmlRegisterSingletonType<PreflightSettingsManager>("com.uav.preflight", 1, 0, "PreflightSettingsManager",
        [](QQmlEngine *engine, QJSEngine *jsEngine) -> QObject * {
            Q_UNUSED(jsEngine)
            return new PreflightSettingsManager(engine);
        });

    if (_weatherProvider) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("WeatherProvider"), _weatherProvider);
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
    qmlEngine->rootContext()->setContextProperty(QStringLiteral("Database"), &DatabaseManager::instance());

    for (int i = 0; i < 8; ++i) {
        QString name = QStringLiteral("CatModel%1").arg(i);
        qmlEngine->rootContext()->setContextProperty(name, _categoryModels[i]);
    }

    qCDebug(preflightPluginLog) << "PreflightPlugin: QML context properties and singletons registered";
    return qmlEngine;
}

void PreflightPlugin::_onActiveVehicleChanged(Vehicle *vehicle)
{
    qCDebug(preflightPluginLog) << "PreflightPlugin: Active vehicle changed"
                                << (vehicle ? vehicle->id() : -1);

    if (vehicle) {
        _setupForVehicle(vehicle);
    } else {
        _telemetryBridge->setVehicle(nullptr);
        _weatherRefreshTimer.stop();
    }
}

void PreflightPlugin::_setupForVehicle(Vehicle *vehicle)
{
    if (!vehicle || !_telemetryBridge || !_preflightManager) return;

    _telemetryBridge->setVehicle(vehicle);
    if (_hardwareTestController) {
        _hardwareTestController->setVehicle(vehicle);
    }
    _preflightManager->startEvaluation(1000);

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

void PreflightPlugin::_refreshWeather()
{
    if (!_weatherProvider || !_telemetryBridge) return;

    auto *settings = PreflightSettingsManager::instance();
    if (!settings || !settings->autoWeatherEnabled()) return;

    QString icao = settings->defaultIcao();
    if (!icao.isEmpty()) {
        _weatherProvider->refreshAll(icao);
    } else if (_telemetryBridge->vehicle()) {
        double lat = _telemetryBridge->vehicle()->latitude();
        double lon = _telemetryBridge->vehicle()->longitude();
        if (qAbs(lat) > 0.01 || qAbs(lon) > 0.01)
            _weatherProvider->fetchWeather(lat, lon);
    }
}

void PreflightPlugin::_startWeatherRefresh()
{
    auto *settings = PreflightSettingsManager::instance();
    int interval = settings ? settings->weatherUpdateIntervalMin() : 5;
    _weatherRefreshTimer.start(interval * 60 * 1000);
}

const QVariantList &PreflightPlugin::analyzePages()
{
    if (_analyzePages.isEmpty()) {
        const QVariantList &basePages = QGCCorePlugin::analyzePages();
        for (const auto &page : basePages) {
            _analyzePages.append(page);
        }

#ifdef QT_DEBUG
        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Test Page"),
                QUrl(QStringLiteral("qrc:/qml/cpts/TestAnalyzePage.qml")),
                QUrl(),
                this)));
        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Module Test"),
                QUrl(QStringLiteral("qrc:/qml/cpts/TestModuleImport.qml")),
                QUrl(),
                this)));
#endif

        _analyzePages.append(QVariant::fromValue(
            new QmlComponentInfo(
                tr("Preflight Checklist"),
                QUrl(QStringLiteral("qrc:/qml/cpts/PreflightChecklistView.qml")),
                QUrl(),
                this)));
    }
    return _analyzePages;
}

void PreflightPlugin::paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo)
{
    Q_UNUSED(colorName)
    Q_UNUSED(colorInfo)
}

bool PreflightPlugin::mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle)
    Q_UNUSED(link)
    Q_UNUSED(message)
    return true;
}

const QVariantList &PreflightPlugin::toolBarIndicators()
{
    if (_toolBarIndicators.isEmpty()) {
        _toolBarIndicators = QVariantList({
            QVariant::fromValue(QUrl("qrc:/custom/PreflightToolbarIndicator.qml")),
        });
    }
    return _toolBarIndicators;
}
