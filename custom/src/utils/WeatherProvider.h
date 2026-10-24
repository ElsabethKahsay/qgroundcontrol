#pragma once

// ============================================================================
// WeatherProvider — Fetches aviation weather data (METAR, TAF, NOTAMs)
// and general forecast data for the UAV Preflight Checklist plugin.
//
// Data sources:
//   - Open-Meteo API: general forecast (temperature, wind, visibility)
//   - NOAA Aviation Weather Center: METAR (current conditions) and
//     TAF (Terminal Aerodrome Forecast)
//   - FAA NOTAM Search: airspace notices and restrictions
//
// The class exposes Q_PROPERTY values that QML binds to directly.
// Network responses are cached on disk (50 MB limit) to reduce
// redundant requests.  All network calls use a 4-second timeout.
//
// METAR freshness is considered valid for 60 minutes after observation.
// TAF analysis flags deteriorating conditions (IFR, strong wind,
// convection) via the tafDeteriorating property.
// ============================================================================

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkDiskCache>
#include <QVariantList>
#include <QDateTime>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QtMath>

class WeatherProvider : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double temperature READ temperature NOTIFY weatherUpdated)
    Q_PROPERTY(double windSpeed READ windSpeed NOTIFY weatherUpdated)
    Q_PROPERTY(double windGust READ windGust NOTIFY weatherUpdated)
    Q_PROPERTY(double windDirection READ windDirection NOTIFY weatherUpdated)
    Q_PROPERTY(int weatherCode READ weatherCode NOTIFY weatherUpdated)
    Q_PROPERTY(QString weatherDescription READ weatherDescription NOTIFY weatherUpdated)
    Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY weatherUpdated)
    Q_PROPERTY(double visibility READ visibility NOTIFY weatherUpdated)
    Q_PROPERTY(double humidity READ humidity NOTIFY weatherUpdated)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY loadingChanged)
    Q_PROPERTY(QDateTime metarTimestamp READ metarTimestamp NOTIFY weatherUpdated)
    Q_PROPERTY(bool metarFresh READ metarFresh NOTIFY weatherUpdated)
    Q_PROPERTY(QStringList precipitation READ precipitation NOTIFY weatherUpdated)
    Q_PROPERTY(QString tafString READ tafString NOTIFY tafUpdated)
    Q_PROPERTY(bool tafDeteriorating READ tafDeteriorating NOTIFY tafUpdated)
    Q_PROPERTY(QStringList tafSummary READ tafSummary NOTIFY tafUpdated)
    // METAR-specific
    Q_PROPERTY(double visibilityKm READ visibilityKm NOTIFY weatherUpdated)
    Q_PROPERTY(int ceilingFt READ ceilingFt NOTIFY weatherUpdated)
    Q_PROPERTY(QString metarString READ metarString NOTIFY weatherUpdated)
    Q_PROPERTY(QStringList metarParsed READ metarParsed NOTIFY weatherUpdated)
    Q_PROPERTY(QVariantList notams READ notams NOTIFY notamsChanged)
    // Display-friendly summaries for QML weather panels
    Q_PROPERTY(QString windSummary READ windSummary NOTIFY weatherUpdated)
    Q_PROPERTY(QString ceiling READ ceiling NOTIFY weatherUpdated)
    Q_PROPERTY(QString stationId READ stationId NOTIFY weatherUpdated)
    Q_PROPERTY(QString rawMetar READ rawMetar NOTIFY weatherUpdated)
    Q_PROPERTY(bool windOk READ windOk NOTIFY weatherUpdated)
    Q_PROPERTY(QString lastUpdated READ lastUpdated NOTIFY weatherUpdated)
    Q_PROPERTY(bool isFetching READ isFetching NOTIFY loadingChanged)
    /// Where the shown values come from, e.g. "METAR HAAB, observed 09:00Z"
    /// or "Forecast (Open-Meteo), fetched 12:00Z"; empty before any data.
    Q_PROPERTY(QString weatherSource READ weatherSource NOTIFY weatherUpdated)

public:
    static WeatherProvider *instance();

    explicit WeatherProvider(QObject *parent = nullptr);
    ~WeatherProvider() override;

    double temperature() const { return m_temperature; }
    double windSpeed() const { return m_windSpeed; }
    double windGust() const { return m_windGust; }
    double windDirection() const { return m_windDirection; }
    int weatherCode() const { return m_weatherCode; }
    QString weatherDescription() const;
    QString currentSummary() const;
    double visibility() const { return m_visibility; }
    double humidity() const { return m_humidity; }
    bool loading() const { return m_loading; }
    QString lastError() const { return m_lastError; }
    QDateTime metarTimestamp() const { return m_metarTimestamp; }
    bool metarFresh() const;
    QStringList precipitation() const { return m_precipitation; }
    QString tafString() const { return m_tafString; }
    bool tafDeteriorating() const { return m_tafDeteriorating; }
    QStringList tafSummary() const { return m_tafSummary; }
    double visibilityKm() const { return m_visibilityKm; }
    int ceilingFt() const { return m_ceilingFt; }
    QString metarString() const { return m_metarString; }
    QStringList metarParsed() const { return m_metarParsed; }
    QVariantList notams() const { return m_notams; }
    QString windSummary() const;
    QString ceiling() const;
    QString stationId() const { return m_stationId; }
    QString rawMetar() const { return m_metarString; }
    bool windOk() const;
    QString lastUpdated() const;
    bool isFetching() const { return m_loading; }
    QString weatherSource() const;

    Q_INVOKABLE void fetchWeather(double latitude, double longitude);
    Q_INVOKABLE void fetchMetar(const QString &icaoCode);
    Q_INVOKABLE void fetchTaf(const QString &icaoCode);
    Q_INVOKABLE void fetchNotam(const QString &icaoCode);
    Q_INVOKABLE void loadNotamGeoJson(const QString &filePath);
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void refreshAll(const QString &icaoCode);
    static QString lookupIcao(double latitude, double longitude, QString *errorMessage = nullptr);

signals:
    void weatherUpdated();
    void loadingChanged();
    void notamsChanged();
    void tafUpdated();

private:
    friend class WeatherProviderTest;

    static constexpr double kMaxWindMps = 10.0;

    QString describeCode(int code) const;
    void handleMetarJson(const QByteArray &data);
    void handleTafJson(const QByteArray &data);
    void handleNotamJson(const QByteArray &data);
    void parsePrecipitation(const QString &metar);
    void handleForecastJson(const QByteArray &data);

    // The current aviationweather.gov METAR JSON format.
    /// obsTime is Unix seconds (an ISO string is still accepted).
    static QDateTime parseObsTime(const QJsonObject &metar);
    /// visib is statute miles as a number or a string such as "10+"; NaN if absent.
    static double parseVisibilitySm(const QJsonValue &visib);
    /// Ceiling = base of the lowest BKN / OVC / OVX layer in clouds[]; -1 = none.
    static int ceilingFromClouds(const QJsonArray &clouds);

    static WeatherProvider *s_instance;

    QNetworkAccessManager m_nam;
    QNetworkDiskCache *m_cache = nullptr;

    // A value nobody reported is NaN (ceiling -1), never a real-looking 0.
    double m_temperature = qQNaN();
    double m_windSpeed = qQNaN();
    double m_windGust = qQNaN();
    double m_windDirection = qQNaN();   ///< NaN also for variable (VRB) wind
    int m_weatherCode = 0;
    QString m_flightCategory;   ///< METAR flight category (VFR/MVFR/IFR/LIFR); empty for Open-Meteo data
    double m_visibility = qQNaN();
    double m_humidity = qQNaN();
    bool m_loading = false;
    QString m_lastError;

    // METAR
    double m_visibilityKm = qQNaN();
    int m_ceilingFt = -1;          ///< -1 = no ceiling reported / unknown
    QString m_metarString;
    QDateTime m_metarTimestamp;    ///< Set ONLY from a METAR's observation time
    QDateTime m_forecastTimestamp; ///< when the Open-Meteo forecast was fetched
    bool m_fromMetar = false;      ///< The shown values are the METAR's
    QStringList m_metarParsed;
    QStringList m_precipitation;
    QString m_stationId;

    // TAF
    QString m_tafString;
    bool m_tafDeteriorating = false;
    QStringList m_tafSummary;

    // NOTAM
    QVariantList m_notams;
};
