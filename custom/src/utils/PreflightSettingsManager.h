#pragma once

// ============================================================================
// PreflightSettingsManager — Persists user-configurable preflight settings
// using QSettings (platform-native storage: registry on Windows, plist on
// macOS, INI on Linux).
//
// Settings groups:
//   Pilot info:     pilot name, license number, aircraft registration
//   Compliance:     FAA Part 107 mode toggle
//   Video:          recording preferences, stream URL override
//   Weather:        auto-fetch toggle, default ICAO station, thresholds
//                   for wind, visibility, and ceiling alerts
//
// All properties are Q_PROPERTY bindings for direct use in QML.
// Setters clamp numeric values to safe ranges before persisting.
// ============================================================================

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

    // Payload release (see core/PayloadRelease.h).
    /// The release servo output (SERVOn), 1-16; 0 = not set (nothing detected).
    /// On the 22-inch copter output 9 or 10.
    Q_PROPERTY(int payloadDropOutput READ payloadDropOutput WRITE setPayloadDropOutput NOTIFY payloadDropChanged)
    /// The payload is released when the output is low (below the middle).
    Q_PROPERTY(bool payloadDropOnWhenLow READ payloadDropOnWhenLow WRITE setPayloadDropOnWhenLow NOTIFY payloadDropChanged)
    /// How many pieces the payload is in: 1 (e.g. 4 kg) or 2 (e.g. 2 × 2 kg).
    Q_PROPERTY(int payloadCount READ payloadCount WRITE setPayloadCount NOTIFY payloadDropChanged)
    /// The mission item where the pilot drops on the RC, for the estimate
    /// before take-off; 0 = not given (the payload is costed as carried home).
    Q_PROPERTY(int payloadDropItem READ payloadDropItem WRITE setPayloadDropItem NOTIFY payloadDropChanged)

public:
    static PreflightSettingsManager *instance();

    explicit PreflightSettingsManager(QObject *parent = nullptr);
    ~PreflightSettingsManager() override;

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

    int payloadDropOutput() const;
    void setPayloadDropOutput(int v);
    bool payloadDropOnWhenLow() const;
    void setPayloadDropOnWhenLow(bool v);
    int payloadCount() const;
    void setPayloadCount(int v);
    int payloadDropItem() const;
    void setPayloadDropItem(int v);

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
    void payloadDropChanged();

private:
    static PreflightSettingsManager *s_instance;
    QSettings m_settings;
};
