#pragma once

#include <QObject>
#include <QSettings>

class PreflightSettingsManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pilotName READ pilotName WRITE setPilotName NOTIFY pilotNameChanged)
    Q_PROPERTY(QString pilotLicense READ pilotLicense WRITE setPilotLicense NOTIFY pilotLicenseChanged)
    Q_PROPERTY(QString aircraftReg READ aircraftReg WRITE setAircraftReg NOTIFY aircraftRegChanged)
    Q_PROPERTY(bool faaPart107Mode READ faaPart107Mode WRITE setFaaPart107Mode NOTIFY faaPart107ModeChanged)

    Q_PROPERTY(bool videoRequiredForPass READ videoRequiredForPass WRITE setVideoRequiredForPass NOTIFY videoRequiredForPassChanged)
    Q_PROPERTY(bool autoRecordOnArm READ autoRecordOnArm WRITE setAutoRecordOnArm NOTIFY autoRecordOnArmChanged)
    Q_PROPERTY(bool showTelemetryOverlay READ showTelemetryOverlay WRITE setShowTelemetryOverlay NOTIFY showTelemetryOverlayChanged)
    Q_PROPERTY(QString videoStreamUrlOverride READ videoStreamUrlOverride WRITE setVideoStreamUrlOverride NOTIFY videoStreamUrlOverrideChanged)

    Q_PROPERTY(bool autoWeatherEnabled READ autoWeatherEnabled WRITE setAutoWeatherEnabled NOTIFY autoWeatherEnabledChanged)
    Q_PROPERTY(QString defaultIcao READ defaultIcao WRITE setDefaultIcao NOTIFY defaultIcaoChanged)
    Q_PROPERTY(double windThresholdSustained READ windThresholdSustained WRITE setWindThresholdSustained NOTIFY windThresholdSustainedChanged)
    Q_PROPERTY(double windThresholdGust READ windThresholdGust WRITE setWindThresholdGust NOTIFY windThresholdGustChanged)
    Q_PROPERTY(double visibilityThresholdKm READ visibilityThresholdKm WRITE setVisibilityThresholdKm NOTIFY visibilityThresholdKmChanged)
    Q_PROPERTY(double ceilingThresholdM READ ceilingThresholdM WRITE setCeilingThresholdM NOTIFY ceilingThresholdMChanged)
    Q_PROPERTY(int weatherUpdateIntervalMin READ weatherUpdateIntervalMin WRITE setWeatherUpdateIntervalMin NOTIFY weatherUpdateIntervalMinChanged)

public:
    static PreflightSettingsManager *instance();

    explicit PreflightSettingsManager(QObject *parent = nullptr);

    QString pilotName() const;
    void setPilotName(const QString &v);

    QString pilotLicense() const;
    void setPilotLicense(const QString &v);

    QString aircraftReg() const;
    void setAircraftReg(const QString &v);

    bool faaPart107Mode() const;
    void setFaaPart107Mode(bool v);

    bool videoRequiredForPass() const;
    void setVideoRequiredForPass(bool v);

    bool autoRecordOnArm() const;
    void setAutoRecordOnArm(bool v);

    bool showTelemetryOverlay() const;
    void setShowTelemetryOverlay(bool v);

    QString videoStreamUrlOverride() const;
    void setVideoStreamUrlOverride(const QString &v);

    bool autoWeatherEnabled() const;
    void setAutoWeatherEnabled(bool v);

    QString defaultIcao() const;
    void setDefaultIcao(const QString &v);

    double windThresholdSustained() const;
    void setWindThresholdSustained(double v);

    double windThresholdGust() const;
    void setWindThresholdGust(double v);

    double visibilityThresholdKm() const;
    void setVisibilityThresholdKm(double v);

    double ceilingThresholdM() const;
    void setCeilingThresholdM(double v);

    int weatherUpdateIntervalMin() const;
    void setWeatherUpdateIntervalMin(int v);

signals:
    void pilotNameChanged();
    void pilotLicenseChanged();
    void aircraftRegChanged();
    void faaPart107ModeChanged();
    void videoRequiredForPassChanged();
    void autoRecordOnArmChanged();
    void showTelemetryOverlayChanged();
    void videoStreamUrlOverrideChanged();
    void autoWeatherEnabledChanged();
    void defaultIcaoChanged();
    void windThresholdSustainedChanged();
    void windThresholdGustChanged();
    void visibilityThresholdKmChanged();
    void ceilingThresholdMChanged();
    void weatherUpdateIntervalMinChanged();

private:
    static PreflightSettingsManager *s_instance;
    QSettings m_settings;
};
