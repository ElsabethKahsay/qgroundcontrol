#pragma once
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "QGCPalette.h"
#include <QTimer>

/// @file PreflightPlugin.h

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
class Vehicle;

class PreflightPlugin : public QGCCorePlugin
{
    Q_OBJECT
    QML_UNCREATABLE("")
public:
    explicit PreflightPlugin(QObject *parent = nullptr);
    ~PreflightPlugin();

    static PreflightPlugin *instance();

    void init() override;

    const QVariantList &analyzePages() override;
    const QVariantList &toolBarIndicators() override;

    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) override;

    void paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo) override;

    bool mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message) override;

private slots:
    void _onActiveVehicleChanged(Vehicle *vehicle);
    void _refreshWeather();

private:
    void _setupForVehicle(Vehicle *vehicle);
    void _startWeatherRefresh();

    QVariantList _analyzePages;
    QVariantList _toolBarIndicators;
    PreflightManager *_preflightManager = nullptr;
    PreflightChecklistModel *_checklistModel = nullptr;
    PreflightChecklistFilterModel *_categoryModels[8] = {};
    TelemetryBridge *_telemetryBridge = nullptr;
    ArmingGate *_armingGate = nullptr;
    WeatherProvider *_weatherProvider = nullptr;
    PreflightSettingsManager *_preflightSettingsManager = nullptr;
    ExportHelper *_exportHelper = nullptr;
    PowerModel *_powerModel = nullptr;
    HardwareTestController *_hardwareTestController = nullptr;
    VehicleProfileManager *_vehicleProfileManager = nullptr;
    QTimer _weatherRefreshTimer;
};
