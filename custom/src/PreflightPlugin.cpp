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
int qInitResources_qmlcache_MainWindowModule();
int qInitResources_qmlcache_QGroundControlControlsModule();

int qInitResources_qmlcache_VehicleSetupModule();

#include "AbstractCheck.h"
#include "adapters/TelemetryBridge.h"
#include "core/ArmingGate.h"
#include "core/BatteryHealthCheck.h"
#include "core/ChecklistEngine.h"
#include "core/ChecklistItemModel.h"
#include "core/MissionEnergyCheck.h"
#include "core/PowerModel.h"
#include "core/VehicleProfileManager.h"
#include "utils/DatabaseManager.h"
#include "PreflightChecklistFilterModel.h"
#include "PreflightChecklistModel.h"
#include "PreflightManager.h"
#include "PreflightSettingsManager.h"
#include "controllers/HardwareTestController.h"
#include "utils/Config.h"
#include "detection/VehicleRegistry.h"
#include "core/AbstractCheck.h"
#include "utils/ExportHelper.h"
#include "utils/WeatherProvider.h"

Q_LOGGING_CATEGORY(preflightPluginLog, "preflight.plugin")

Q_APPLICATION_STATIC(PreflightPlugin, _preflightPluginInstance)

extern int qInitResources_custom();

PreflightPlugin::PreflightPlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
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

void PreflightPlugin::init()
{
    QGCCorePlugin::init();

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
    _preflightSettingsManager = new PreflightSettingsManager(this);
    connect(&_weatherRefreshTimer, &QTimer::timeout, this, &PreflightPlugin::_refreshWeather);

    _hardwareTestController = new HardwareTestController(this);

    _powerModel = new PowerModel(this);
    _exportHelper = new ExportHelper(this);

    DatabaseManager::instance().initialize();

    QCoreApplication::setApplicationName(QStringLiteral("Skywin GCS"));

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

    _checklistItemModel = new ChecklistItemModel(this);
    _populateChecklistModel();
    _checklistEngine = new ChecklistEngine(this);
    _checklistEngine->setModel(_checklistItemModel);
    _checklistEngine->setTelemetryBridge(_telemetryBridge);

    connect(_telemetryBridge, &TelemetryBridge::batteryVoltageChanged, this, [this]() {
        if (!_telemetryBridge || !_vehicleProfileManager) return;
        if (!_vehicleProfileManager->currentBatterySerial().isEmpty()) return;
        QString sysId = QStringLiteral("batt:%1").arg(_telemetryBridge->vehicle() ? _telemetryBridge->vehicle()->id() : 0);
        _vehicleProfileManager->setBatterySerial(sysId);
    });

    auto *registry = VehicleRegistry::instance();
    connect(registry, &VehicleRegistry::knownVehicleConnected,
            this, &PreflightPlugin::_onKnownVehicleConnected);
    connect(registry, &VehicleRegistry::newVehicleRegistered,
            this, &PreflightPlugin::_onNewVehicleRegistered);

    connect(_armingGate, &ArmingGate::gateOpened,
            this, &PreflightPlugin::_onGateOpened);
    connect(_armingGate, &ArmingGate::gateClosed,
            this, &PreflightPlugin::_onGateClosed);

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
            Q_UNUSED(engine) Q_UNUSED(jsEngine)
            return PreflightSettingsManager::instance();
        });

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
    if (_checklistItemModel) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("PreflightModel"), _checklistItemModel);
    }
    if (_checklistEngine) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ChecklistEngine"), _checklistEngine);
    }
    qmlEngine->rootContext()->setContextProperty(QStringLiteral("VehicleRegistry"), VehicleRegistry::instance());
    qmlEngine->rootContext()->setContextProperty(QStringLiteral("Database"), &DatabaseManager::instance());

    for (int i = 0; i < 8; ++i) {
        QString name = QStringLiteral("CatModel%1").arg(i);
        qmlEngine->rootContext()->setContextProperty(name, _categoryModels[i]);
    }

    qCDebug(preflightPluginLog) << "PreflightPlugin: QML context properties and singletons registered";
    return qmlEngine;
}

void PreflightPlugin::_populateChecklistModel()
{
    if (!_checklistItemModel || !_preflightManager)
        return;
    QJsonArray items;
    const auto checks = _preflightManager->checks();
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
    for (auto it = config.begin(); it != config.end(); ++it) {
        AbstractCheck *check = _preflightManager->checkById(it.key());
        if (check) {
            QJsonObject checkConfig = it.value().toObject();
            check->applyVehicleConfig(checkConfig);
        }
    }
    qCDebug(preflightPluginLog) << "Vehicle config loaded for fingerprint" << fingerprint.left(16);
}

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

void PreflightPlugin::_onGateOpened()
{
    if (_currentSessionId >= 0) return;
    QString fp = VehicleRegistry::instance()->currentFingerprint();
    if (fp.isEmpty()) return;
    _currentSessionId = DatabaseManager::instance().startFlightSession(fp, QString());
    _sessionStartTime = QDateTime::currentDateTime();
    qCDebug(preflightPluginLog) << "Flight session started:" << _currentSessionId;
}

void PreflightPlugin::_onGateClosed(const QString &reason)
{
    Q_UNUSED(reason)
    if (_currentSessionId < 0) return;
    double duration = _sessionStartTime.isValid()
        ? _sessionStartTime.secsTo(QDateTime::currentDateTime())
        : 0.0;
    DatabaseManager::instance().endFlightSession(_currentSessionId, duration);
    DatabaseManager::instance().incrementBatteryCycle(_currentSessionId);
    qCDebug(preflightPluginLog) << "Flight session ended:" << _currentSessionId << "duration:" << duration << "s";
    _currentSessionId = -1;
}

void PreflightPlugin::_setupForVehicle(Vehicle *vehicle)
{
    if (!vehicle || !_telemetryBridge || !_preflightManager) return;

    _telemetryBridge->setVehicle(vehicle);
    if (_hardwareTestController) {
        _hardwareTestController->setVehicle(vehicle);
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
#endif

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
    }
    return _analyzePages;
}

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
