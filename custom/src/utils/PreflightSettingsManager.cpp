#include "PreflightSettingsManager.h"

PreflightSettingsManager *PreflightSettingsManager::s_instance = nullptr;

PreflightSettingsManager *PreflightSettingsManager::instance()
{
    return s_instance;
}

PreflightSettingsManager::PreflightSettingsManager(QObject *parent)
    : QObject(parent)
    , m_settings("UAVPreflight", "UAVPreflight")  // org/app name for QSettings
{
    s_instance = this;
}

PreflightSettingsManager::~PreflightSettingsManager()
{
    if (s_instance == this)
        s_instance = nullptr;
}

// ── Pilot info ───────────────────────────────────────────────────────

QString PreflightSettingsManager::pilotName() const
{
    return m_settings.value("pilotName", "").toString();
}

void PreflightSettingsManager::setPilotName(const QString &v)
{
    if (v != pilotName()) {
        m_settings.setValue("pilotName", v);
        emit pilotNameChanged();
    }
}

QString PreflightSettingsManager::pilotLicense() const
{
    return m_settings.value("pilotLicense", "").toString();
}

void PreflightSettingsManager::setPilotLicense(const QString &v)
{
    if (v != pilotLicense()) {
        m_settings.setValue("pilotLicense", v);
        emit pilotLicenseChanged();
    }
}

QString PreflightSettingsManager::aircraftReg() const
{
    return m_settings.value("aircraftReg", "").toString();
}

void PreflightSettingsManager::setAircraftReg(const QString &v)
{
    if (v != aircraftReg()) {
        m_settings.setValue("aircraftReg", v);
        emit aircraftRegChanged();
    }
}

// ── Compliance ───────────────────────────────────────────────────────

bool PreflightSettingsManager::faaPart107Mode() const
{
    return m_settings.value("faaPart107mode", false).toBool();
}

void PreflightSettingsManager::setFaaPart107Mode(bool v)
{
    if (v != faaPart107Mode()) {
        m_settings.setValue("faaPart107mode", v);
        emit faaPart107ModeChanged();
    }
}

// ── Video settings ───────────────────────────────────────────────────

bool PreflightSettingsManager::videoRequiredForPass() const
{
    return m_settings.value("videoRequiredForPass", false).toBool();
}

void PreflightSettingsManager::setVideoRequiredForPass(bool v)
{
    if (v != videoRequiredForPass()) {
        m_settings.setValue("videoRequiredForPass", v);
        emit videoRequiredForPassChanged();
    }
}

bool PreflightSettingsManager::autoRecordOnArm() const
{
    return m_settings.value("autoRecordOnArm", false).toBool();
}

void PreflightSettingsManager::setAutoRecordOnArm(bool v)
{
    if (v != autoRecordOnArm()) {
        m_settings.setValue("autoRecordOnArm", v);
        emit autoRecordOnArmChanged();
    }
}

bool PreflightSettingsManager::showTelemetryOverlay() const
{
    return m_settings.value("showTelemetryOverlay", true).toBool();
}

void PreflightSettingsManager::setShowTelemetryOverlay(bool v)
{
    if (v != showTelemetryOverlay()) {
        m_settings.setValue("showTelemetryOverlay", v);
        emit showTelemetryOverlayChanged();
    }
}

QString PreflightSettingsManager::videoStreamUrlOverride() const
{
    return m_settings.value("videoStreamUrlOverride", "").toString();
}

void PreflightSettingsManager::setVideoStreamUrlOverride(const QString &v)
{
    if (v != videoStreamUrlOverride()) {
        m_settings.setValue("videoStreamUrlOverride", v);
        emit videoStreamUrlOverrideChanged();
    }
}

// ── Weather settings ─────────────────────────────────────────────────
// Thresholds for automated weather-based go/no-go decisions.
// Values are clamped to safe ranges in setters.

bool PreflightSettingsManager::autoWeatherEnabled() const
{
    return m_settings.value("autoWeatherEnabled", true).toBool();
}

void PreflightSettingsManager::setAutoWeatherEnabled(bool v)
{
    if (v != autoWeatherEnabled()) {
        m_settings.setValue("autoWeatherEnabled", v);
        emit autoWeatherEnabledChanged();
    }
}

QString PreflightSettingsManager::defaultIcao() const
{
    // Addis Ababa Bole, the airport nearest the base; changeable in Preflight
    // Settings, and an empty entry turns the METAR off.
    return m_settings.value("defaultIcao", "HAAB").toString();
}

void PreflightSettingsManager::setDefaultIcao(const QString &v)
{
    if (v != defaultIcao()) {
        m_settings.setValue("defaultIcao", v.toUpper().trimmed());
        emit defaultIcaoChanged();
    }
}

double PreflightSettingsManager::windThresholdSustained() const
{
    return m_settings.value("windThresholdSustained", 8.0).toDouble();
}

void PreflightSettingsManager::setWindThresholdSustained(double v)
{
    v = qBound(1.0, v, 30.0);
    if (qAbs(v - windThresholdSustained()) > 0.1) {
        m_settings.setValue("windThresholdSustained", v);
        emit windThresholdSustainedChanged();
    }
}

double PreflightSettingsManager::windThresholdGust() const
{
    // The operators' gust limit for the 22-inch copter: anything above 12 m/s
    // turns the weather red. Changeable in Preflight Settings.
    return m_settings.value("windThresholdGust", 12.0).toDouble();
}

void PreflightSettingsManager::setWindThresholdGust(double v)
{
    v = qBound(1.0, v, 35.0);
    if (qAbs(v - windThresholdGust()) > 0.1) {
        m_settings.setValue("windThresholdGust", v);
        emit windThresholdGustChanged();
    }
}

double PreflightSettingsManager::visibilityThresholdKm() const
{
    return m_settings.value("visibilityThresholdKm", 5.0).toDouble();
}

void PreflightSettingsManager::setVisibilityThresholdKm(double v)
{
    v = qBound(0.1, v, 50.0);
    if (qAbs(v - visibilityThresholdKm()) > 0.01) {
        m_settings.setValue("visibilityThresholdKm", v);
        emit visibilityThresholdKmChanged();
    }
}

double PreflightSettingsManager::ceilingThresholdM() const
{
    return m_settings.value("ceilingThresholdM", 150.0).toDouble();
}

void PreflightSettingsManager::setCeilingThresholdM(double v)
{
    v = qBound(0.0, v, 3000.0);
    if (qAbs(v - ceilingThresholdM()) > 1.0) {
        m_settings.setValue("ceilingThresholdM", v);
        emit ceilingThresholdMChanged();
    }
}

int PreflightSettingsManager::weatherUpdateIntervalMin() const
{
    return m_settings.value("weatherUpdateIntervalMin", 5).toInt();
}

void PreflightSettingsManager::setWeatherUpdateIntervalMin(int v)
{
    v = qBound(1, v, 15);
    if (v != weatherUpdateIntervalMin()) {
        m_settings.setValue("weatherUpdateIntervalMin", v);
        emit weatherUpdateIntervalMinChanged();
    }
}

// ── Payload release ─────────────────────────────────────────────────

int PreflightSettingsManager::payloadDropOutput() const
{
    return m_settings.value("payloadDropOutput", 0).toInt();
}

void PreflightSettingsManager::setPayloadDropOutput(int v)
{
    v = qBound(0, v, 16);
    if (v != payloadDropOutput()) {
        m_settings.setValue("payloadDropOutput", v);
        emit payloadDropChanged();
    }
}

bool PreflightSettingsManager::payloadDropOnWhenLow() const
{
    return m_settings.value("payloadDropOnWhenLow", false).toBool();
}

void PreflightSettingsManager::setPayloadDropOnWhenLow(bool v)
{
    if (v != payloadDropOnWhenLow()) {
        m_settings.setValue("payloadDropOnWhenLow", v);
        emit payloadDropChanged();
    }
}

int PreflightSettingsManager::payloadCount() const
{
    return m_settings.value("payloadCount", 1).toInt();
}

void PreflightSettingsManager::setPayloadCount(int v)
{
    v = qBound(1, v, 2);
    if (v != payloadCount()) {
        m_settings.setValue("payloadCount", v);
        emit payloadDropChanged();
    }
}

int PreflightSettingsManager::payloadDropItem() const
{
    return m_settings.value("payloadDropItem", 0).toInt();
}

void PreflightSettingsManager::setPayloadDropItem(int v)
{
    v = qMax(0, v);
    if (v != payloadDropItem()) {
        m_settings.setValue("payloadDropItem", v);
        emit payloadDropChanged();
    }
}
