#include "WeatherProvider.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>
#include <QUrlQuery>
#include <QTimeZone>

#include <cmath>

#include "utils/Config.h"

WeatherProvider *WeatherProvider::s_instance = nullptr;

WeatherProvider *WeatherProvider::instance()
{
    return s_instance;
}

WeatherProvider::WeatherProvider(QObject *parent)
    : QObject(parent)
{
    // Set up a disk cache for weather API responses to avoid redundant requests.
    m_cache = new QNetworkDiskCache(this);
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                       + QLatin1Char('/') + QLatin1String(kWeatherCacheSubdirName);
    QDir().mkpath(cacheDir);
    m_cache->setCacheDirectory(cacheDir);
    m_cache->setMaximumCacheSize(kWeatherCacheMaxBytes); // 50 MB
    m_nam.setCache(m_cache);
    s_instance = this;
}

WeatherProvider::~WeatherProvider()
{
    if (s_instance == this)
        s_instance = nullptr;
}

/** @brief METAR data is considered fresh if observed within the last 60 minutes. */
bool WeatherProvider::metarFresh() const
{
    if (!m_metarTimestamp.isValid())
        return false;
    return m_metarTimestamp.secsTo(QDateTime::currentDateTimeUtc()) < 3600; // 60 min
}

QString WeatherProvider::weatherDescription() const
{
    // METAR data carries a flight category, not a WMO weather code; name it
    // directly instead of describing a stand-in code (LIFR used to read
    // "Thunderstorm").
    if (m_flightCategory == QStringLiteral("VFR"))  return QStringLiteral("VFR");
    if (m_flightCategory == QStringLiteral("MVFR")) return QStringLiteral("MVFR (marginal VFR)");
    if (m_flightCategory == QStringLiteral("IFR"))  return QStringLiteral("IFR");
    if (m_flightCategory == QStringLiteral("LIFR")) return QStringLiteral("LIFR (low IFR)");
    return describeCode(m_weatherCode);
}

// Unknown values print as "--", never as 0.
static QString num(double v, int decimals, double scale = 1.0)
{
    return std::isfinite(v) ? QString::number(v * scale, 'f', decimals) : QStringLiteral("--");
}

QString WeatherProvider::currentSummary() const
{
    if (!m_metarTimestamp.isValid() && !m_forecastTimestamp.isValid())
        return QStringLiteral("Weather not yet fetched");
    return QStringLiteral("Wind %1 kt, Vis %2 km, %3, %4\u00B0C")
        .arg(num(m_windSpeed, 0, 1.0 / 0.514444), num(m_visibilityKm, 1), weatherDescription(),
             num(m_temperature, 1));
}

QString WeatherProvider::weatherSource() const
{
    if (m_fromMetar)
        return QStringLiteral("METAR %1, observed %2")
            .arg(m_stationId.isEmpty() ? QStringLiteral("?") : m_stationId,
                 m_metarTimestamp.isValid() ? m_metarTimestamp.toUTC().toString(QStringLiteral("HH:mm'Z'"))
                                            : QStringLiteral("time unknown"));
    if (m_forecastTimestamp.isValid())
        return QStringLiteral("Forecast (Open-Meteo), fetched %1")
            .arg(m_forecastTimestamp.toUTC().toString(QStringLiteral("HH:mm'Z'")));
    return {};
}

/** @brief Map a WMO weather code (or Open-Meteo code) to a human-readable description. */
QString WeatherProvider::describeCode(int code) const
{
    switch (code) {
    case 0: return QStringLiteral("Clear sky");
    case 1: return QStringLiteral("Mainly clear");
    case 2: return QStringLiteral("Partly cloudy");
    case 3: return QStringLiteral("Overcast");
    case 45: return QStringLiteral("Foggy");
    case 48: return QStringLiteral("Depositing rime fog");
    case 51: return QStringLiteral("Light drizzle");
    case 53: return QStringLiteral("Moderate drizzle");
    case 55: return QStringLiteral("Dense drizzle");
    case 56: return QStringLiteral("Light freezing drizzle");
    case 57: return QStringLiteral("Dense freezing drizzle");
    case 61: return QStringLiteral("Slight rain");
    case 63: return QStringLiteral("Moderate rain");
    case 65: return QStringLiteral("Heavy rain");
    case 66: return QStringLiteral("Light freezing rain");
    case 67: return QStringLiteral("Heavy freezing rain");
    case 71: return QStringLiteral("Slight snow");
    case 73: return QStringLiteral("Moderate snow");
    case 75: return QStringLiteral("Heavy snow");
    case 77: return QStringLiteral("Snow grains");
    case 80: return QStringLiteral("Slight rain showers");
    case 81: return QStringLiteral("Moderate rain showers");
    case 82: return QStringLiteral("Violent rain showers");
    case 85: return QStringLiteral("Slight snow showers");
    case 86: return QStringLiteral("Heavy snow showers");
    case 95: return QStringLiteral("Thunderstorm");
    case 96: return QStringLiteral("Thunderstorm with slight hail");
    case 99: return QStringLiteral("Thunderstorm with heavy hail");
    default: return QStringLiteral("Unknown (%1)").arg(code);
    }
}

void WeatherProvider::clearCache()
{
    m_cache->clear();
}

// ── Open-Meteo forecast ──────────────────────────────────────────────
// Fetches current weather conditions (temp, wind, visibility, humidity)
// from the free Open-Meteo API by lat/lon coordinates.

void WeatherProvider::fetchWeather(double latitude, double longitude)
{
    m_loading = true;
    m_lastError.clear();
    emit loadingChanged();

    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(latitude, 'f', 6));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(longitude, 'f', 6));
    query.addQueryItem(QStringLiteral("current"),
        QStringLiteral("temperature_2m,relative_humidity_2m,weather_code,"
                       "wind_speed_10m,wind_direction_10m,wind_gusts_10m,visibility"));
    // Open-Meteo defaults to km/h; m_windSpeed is m/s everywhere else
    // (METAR path converts kt -> m/s, display converts m/s -> kt).
    query.addQueryItem(QStringLiteral("wind_speed_unit"), QStringLiteral("ms"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::ContentNotFoundError)
                return;
            m_lastError = reply->errorString();
            emit loadingChanged();
            return;
        }

        handleForecastJson(reply->readAll());
        emit loadingChanged();
    });
}

// A forecast never stamps the METAR time, never overwrites a fresh
// METAR (an observation beats a model), and a missing field is NaN, not 0.
void WeatherProvider::handleForecastJson(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        m_lastError = QStringLiteral("Invalid JSON response");
        return;
    }
    const QJsonObject current = doc.object().value(QStringLiteral("current")).toObject();
    m_forecastTimestamp = QDateTime::currentDateTimeUtc();
    m_lastError.clear();
    if (metarFresh()) {
        emit weatherUpdated();   // the source line now also knows a forecast exists
        return;
    }
    auto val = [&current](const char *key) {
        const QJsonValue v = current.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : qQNaN();
    };
    m_fromMetar = false;
    m_temperature = val("temperature_2m");
    m_humidity = val("relative_humidity_2m");
    m_weatherCode = current.value(QStringLiteral("weather_code")).toInt(-1);
    m_flightCategory.clear();
    m_windSpeed = val("wind_speed_10m");
    m_windDirection = val("wind_direction_10m");
    m_windGust = val("wind_gusts_10m");
    m_visibility = val("visibility");
    m_visibilityKm = m_visibility / 1000.0;
    m_ceilingFt = -1;            // a forecast reports no ceiling
    emit weatherUpdated();
}

// ── METAR (NOAA ADDS) ────────────────────────────────────────────────
// Fetches the latest METAR observation for an ICAO station code.
// Converts knots to m/s for wind and statute miles to km for visibility.

void WeatherProvider::fetchMetar(const QString &icaoCode)
{
    if (icaoCode.trimmed().isEmpty()) return;

    m_stationId = icaoCode.trimmed().toUpper();
    m_loading = true;
    m_lastError.clear();
    emit loadingChanged();

    QUrl url(QStringLiteral("https://aviationweather.gov/api/data/metar"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), icaoCode.trimmed().toUpper());
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("hours"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = reply->errorString();
            emit loadingChanged();
            return;
        }

        handleMetarJson(reply->readAll());
        emit loadingChanged();
    });
}

/**
 * @brief Parse the NOAA METAR JSON response and update all weather properties.
 *
 * Handles unit conversions (knots→m/s, sm→km), derives humidity from
 * temperature/dewpoint using the Magnus formula, maps flight category
 * to a weather code, and extracts ceiling height from cloud layers.
 */
void WeatherProvider::handleMetarJson(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        m_lastError = QStringLiteral("Invalid METAR response");
        return;
    }

    QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        m_lastError = QStringLiteral("No METAR data for station");
        return;
    }

    QJsonObject metar = arr.first().toObject();
    m_metarString = metar.value(QStringLiteral("rawOb")).toString();

    // The observation time; never "now" (that made any METAR fresh).
    m_metarTimestamp = parseObsTime(metar);
    m_fromMetar = true;

    auto field = [&metar](const char *key) {
        const QJsonValue v = metar.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : qQNaN();
    };

    // A field the METAR does not carry is unknown (NaN), not 0.
    m_temperature = field("temp");
    m_windSpeed = field("wspd") * 0.514444;                   // knots -> m/s
    // wdir is degrees, or "VRB" for variable wind (NaN).
    m_windDirection = field("wdir");
    // Gusts: "wgst" (current API; "gust" accepted). A METAR without a G group
    // reports no gusts, so 0 is the real value here.
    if (metar.contains(QStringLiteral("wgst")))
        m_windGust = field("wgst") * 0.514444;
    else if (metar.contains(QStringLiteral("gust")))
        m_windGust = field("gust") * 0.514444;
    else
        m_windGust = std::isfinite(m_windSpeed) ? 0.0 : qQNaN();

    // Visibility: statute miles, a number or a string such as "10+".
    const double visSm = parseVisibilitySm(metar.value(QStringLiteral("visib")));
    m_visibility = visSm * 1609.34;
    m_visibilityKm = visSm * 1.60934;

    // Ceiling: lowest BKN/OVC/OVX layer in clouds[]; the old "ceiling" object
    // is still read if a source sends it.
    m_ceilingFt = ceilingFromClouds(metar.value(QStringLiteral("clouds")).toArray());
    if (metar.value(QStringLiteral("ceiling")).isObject())
        m_ceilingFt = metar.value(QStringLiteral("ceiling")).toObject()
                          .value(QStringLiteral("base_feet_agl")).toInt(m_ceilingFt);

    // Flight category: "fltCat" (current API; "flightCategory" accepted).
    m_flightCategory = metar.value(QStringLiteral("fltCat")).toString();
    if (m_flightCategory.isEmpty())
        m_flightCategory = metar.value(QStringLiteral("flightCategory")).toString();
    m_weatherCode = m_flightCategory == QStringLiteral("VFR")  ? 0
                  : m_flightCategory == QStringLiteral("MVFR") ? 2
                  : (m_flightCategory == QStringLiteral("IFR") || m_flightCategory == QStringLiteral("LIFR"))
                        ? 45     // fog-class code; never 95 (thunderstorm)
                        : -1;
    if (metar.contains(QStringLiteral("icaoId")))
        m_stationId = metar.value(QStringLiteral("icaoId")).toString();
    m_humidity = qQNaN();

    // Humidity from dewpoint
    if (metar.contains(QStringLiteral("temp")) && metar.contains(QStringLiteral("dewp"))) {
        double t = metar.value(QStringLiteral("temp")).toDouble();
        double d = metar.value(QStringLiteral("dewp")).toDouble();
        if (t > -50.0) {
            double e = 6.112 * qExp(17.67 * d / (d + 243.5));
            double es = 6.112 * qExp(17.67 * t / (t + 243.5));
            m_humidity = qBound(0.0, (e / es) * 100.0, 100.0);
        }
    }

    // Parse precip from raw METAR
    parsePrecipitation(m_metarString);

    // Build parsed lines
    m_metarParsed.clear();
    m_metarParsed << QStringLiteral("Wind: %1%2kt from %3")
        .arg(num(m_windSpeed, 0, 1.0 / 0.514444),
             m_windGust > 0.0 ? QStringLiteral(" G%1").arg(num(m_windGust, 0, 1.0 / 0.514444)) : QString(),
             std::isfinite(m_windDirection) ? num(m_windDirection, 0) + QChar(0x00B0) : QStringLiteral("variable"));
    m_metarParsed << QStringLiteral("Vis: %1 km").arg(num(m_visibilityKm, 1));
    m_metarParsed << (m_ceilingFt > 0 ? QStringLiteral("Ceiling: %1 ft AGL").arg(m_ceilingFt)
                                      : QStringLiteral("Ceiling: none"));
    m_metarParsed << QStringLiteral("Temp: %1 \u00B0C").arg(num(m_temperature, 1));
    if (!m_precipitation.isEmpty())
        m_metarParsed << QStringLiteral("Precip: %1").arg(m_precipitation.join(QStringLiteral(", ")));

    emit weatherUpdated();
}

/**
 * @brief Extract precipitation/weather phenomenon codes from a raw METAR string.
 *
 * Matches 2-letter codes (RA, SN, TS, FG, etc.) in the weather phenomena
 * section of the METAR.  Deduplicates results.
 */
void WeatherProvider::parsePrecipitation(const QString &metar)
{
    m_precipitation.clear();
    if (metar.isEmpty()) return;

    // Common METAR precipitation codes in the raw string
    static const QStringList precipCodes = {
        QStringLiteral("RA"), QStringLiteral("DZ"), QStringLiteral("SN"),
        QStringLiteral("SG"), QStringLiteral("PL"), QStringLiteral("GR"),
        QStringLiteral("GS"), QStringLiteral("UP"), QStringLiteral("BR"),
        QStringLiteral("FG"), QStringLiteral("DU"), QStringLiteral("SA"),
        QStringLiteral("HZ"), QStringLiteral("PY"), QStringLiteral("VA"),
        QStringLiteral("FU"), QStringLiteral("TS"), QStringLiteral("SQ"),
        QStringLiteral("FC"), QStringLiteral("SS"), QStringLiteral("DS"),
        QStringLiteral("SH"), QStringLiteral("FZ"), QStringLiteral("BC"),
        QStringLiteral("MI"), QStringLiteral("PR"), QStringLiteral("DR")
    };

    // Match weather phenomena between the wind and sky sections of METAR
    // Typical METAR: KLAX 012053Z 26012KT 10SM -RA FEW040 ... =
    QRegularExpression re(QStringLiteral("(?:[\\-+]?)([A-Z]{2,})"));
    auto it = re.globalMatch(metar);
    while (it.hasNext()) {
        auto match = it.next();
        QString code = match.captured(1);
        if (precipCodes.contains(code) && !m_precipitation.contains(code))
            m_precipitation.append(code);
    }
}

// ── TAF (NOAA ADDS) ─────────────────────────────────────────────────
// Fetches the Terminal Aerodrome Forecast for an ICAO station.
// Analyzes the raw TAF string for deteriorating conditions.

void WeatherProvider::fetchTaf(const QString &icaoCode)
{
    if (icaoCode.trimmed().isEmpty()) return;

    m_loading = true;
    m_lastError.clear();
    emit loadingChanged();

    QUrl url(QStringLiteral("https://aviationweather.gov/api/data/taf"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), icaoCode.trimmed().toUpper());
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("hours"), QStringLiteral("6"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = reply->errorString();
            emit loadingChanged();
            return;
        }

        handleTafJson(reply->readAll());
        emit loadingChanged();
    });
}

/**
 * @brief Parse TAF response and detect deteriorating conditions.
 *
 * Checks for: multiple change groups (instability), IFR/LIFR categories,
 * low ceilings (OVC/BKN below 1000ft), convection (TS, +RA, SN),
 * and strong winds (>20kt sustained or >25kt gusts).
 */
void WeatherProvider::handleTafJson(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        m_lastError = QStringLiteral("Invalid TAF response");
        return;
    }

    QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        m_lastError = QStringLiteral("No TAF data for station");
        return;
    }

    QJsonObject taf = arr.first().toObject();
    m_tafString = taf.value(QStringLiteral("rawTAF")).toString();

    // Check for deterioration in next 2 hours
    m_tafDeteriorating = false;
    m_tafSummary.clear();

    // Parse TAF change groups (FM, TEMPO, PROB)
    QString raw = m_tafString;
    QRegularExpression changeRe(
        QStringLiteral("(TEMPO|FM(\\d{6})|PROB30|PROB40|PROB50)"));
    auto it = changeRe.globalMatch(raw);
    int changeCount = 0;
    while (it.hasNext()) {
        it.next();
        changeCount++;
    }

    if (changeCount > 2) {
        m_tafDeteriorating = true;
        m_tafSummary << QStringLiteral("Multiple change groups indicate instability");
    }

    // Check for IFR/LIFR conditions in TAF body
    bool hasIfr = raw.contains(QStringLiteral("IFR")) ||
                  raw.contains(QStringLiteral("LIFR")) ||
                  raw.contains(QRegularExpression(QStringLiteral("\\b(OVC|BKN)\\d{3}")));
    if (hasIfr) {
        m_tafDeteriorating = true;
        m_tafSummary << QStringLiteral("IFR/LIFR or low ceilings forecast");
    }

    // Check for thunderstorm, rain, snow
    bool hasConvection = raw.contains(QStringLiteral("TS")) ||
                         raw.contains(QStringLiteral("+RA")) ||
                         raw.contains(QStringLiteral("SN"));
    if (hasConvection) {
        m_tafDeteriorating = true;
        m_tafSummary << QStringLiteral("Convection/precipitation forecast");
    }

    // Check strong wind
    QRegularExpression windRe(QStringLiteral("(\\d{2})G?(\\d{2})?KT"));
    auto windMatch = windRe.match(raw);
    if (windMatch.hasMatch()) {
        int sustained = windMatch.captured(1).toInt();
        int gust = windMatch.captured(2).toInt();
        if (sustained > 20 || gust > 25) {
            m_tafDeteriorating = true;
            m_tafSummary << QStringLiteral("Strong wind forecast (%1/%2kt)")
                .arg(sustained).arg(gust);
        }
    }

    if (m_tafSummary.isEmpty())
        m_tafSummary << QStringLiteral("No significant changes in next 6 hours");

    emit tafUpdated();
    emit weatherUpdated();
}

// ── Refresh all ──────────────────────────────────────────────────────
// Convenience: fetches both METAR and TAF for the given station.

void WeatherProvider::refreshAll(const QString &icaoCode)
{
    fetchMetar(icaoCode);
    fetchTaf(icaoCode);
}

// ── NOTAM (FAA) ──────────────────────────────────────────────────────
// Fetches NOTAMs (Notices to Air Missions) from the FAA for a given
// ICAO station.  POSTs a JSON body with the station code.

void WeatherProvider::fetchNotam(const QString &icaoCode)
{
    if (icaoCode.trimmed().isEmpty()) return;

    m_stationId = icaoCode.trimmed().toUpper();
    m_loading = true;
    m_lastError.clear();
    emit loadingChanged();

    QUrl url(QStringLiteral("https://notams.aim.faa.gov/notamSearch/search"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(4000);

    QJsonObject body;
    QJsonArray icaos;
    icaos.append(icaoCode.trimmed().toUpper());
    body[QStringLiteral("icao")] = icaos;
    body[QStringLiteral("notamTypes")] = QJsonArray();
    body[QStringLiteral("locations")] = QJsonArray();

    QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = reply->errorString();
            emit loadingChanged();
            return;
        }

        handleNotamJson(reply->readAll());
        emit loadingChanged();
    });
}

/** @brief Parse FAA NOTAM JSON response into a QVariantList for QML binding. */
void WeatherProvider::handleNotamJson(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_lastError = QStringLiteral("Invalid NOTAM response");
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray results = root.value(QStringLiteral("notamList")).toArray();
    if (results.isEmpty()) {
        results = root.value(QStringLiteral("notams")).toArray();
    }

    m_notams.clear();
    for (const QJsonValue &v : results) {
        QJsonObject n = v.toObject();
        QVariantMap entry;
        entry[QStringLiteral("id")] = n.value(QStringLiteral("notamId")).toString();
        entry[QStringLiteral("type")] = n.value(QStringLiteral("type")).toString();
        entry[QStringLiteral("text")] = n.value(QStringLiteral("message")).toString();
        if (entry[QStringLiteral("text")].toString().isEmpty())
            entry[QStringLiteral("text")] = n.value(QStringLiteral("text")).toString();
        entry[QStringLiteral("start")] = n.value(QStringLiteral("startTime")).toString();
        entry[QStringLiteral("end")] = n.value(QStringLiteral("endTime")).toString();
        m_notams.append(entry);
    }

    emit notamsChanged();
    emit weatherUpdated();
}

// ── Local GeoJSON NOTAM ──────────────────────────────────────────────
// Loads NOTAMs from a local GeoJSON file for offline use or testing.

void WeatherProvider::loadNotamGeoJson(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Cannot open GeoJSON file: ") + filePath;
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_lastError = QStringLiteral("Invalid GeoJSON");
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray features = root.value(QStringLiteral("features")).toArray();

    m_notams.clear();
    for (const QJsonValue &fv : features) {
        QJsonObject feat = fv.toObject();
        QJsonObject props = feat.value(QStringLiteral("properties")).toObject();
        QVariantMap entry;
        entry[QStringLiteral("id")] = props.value(QStringLiteral("id")).toString();
        entry[QStringLiteral("type")] = props.value(QStringLiteral("type")).toString();
        entry[QStringLiteral("text")] = props.value(QStringLiteral("text")).toString();
        entry[QStringLiteral("start")] = props.value(QStringLiteral("start")).toString();
        entry[QStringLiteral("end")] = props.value(QStringLiteral("end")).toString();
        m_notams.append(entry);
    }

    emit notamsChanged();
    if (m_notams.isEmpty())
        m_lastError = QStringLiteral("GeoJSON has no NOTAM features");
}

QString WeatherProvider::windSummary() const
{
    if (!std::isfinite(m_windSpeed))
        return {};
    return QStringLiteral("%1 kt from %2")
        .arg(num(m_windSpeed, 0, 1.0 / 0.514444),
             std::isfinite(m_windDirection) ? num(m_windDirection, 0) + QChar(0x00B0) : QStringLiteral("variable"));
}

QString WeatherProvider::ceiling() const
{
    if (m_ceilingFt > 0)
        return QStringLiteral("%1 ft AGL").arg(m_ceilingFt);
    // No BKN/OVC layer is "no ceiling", not CAVOK (which also means vis >= 10 km).
    if (m_fromMetar)
        return QStringLiteral("None");
    return {};
}

bool WeatherProvider::windOk() const
{
    // Unknown wind is not "OK".
    return std::isfinite(m_windSpeed) && m_windSpeed <= kMaxWindMps;
}

QString WeatherProvider::lastUpdated() const
{
    const QDateTime t = m_fromMetar ? m_metarTimestamp : m_forecastTimestamp;
    if (!t.isValid())
        return {};
    return t.toLocalTime().toString(QStringLiteral("dd MMM yyyy  HH:mm"));
}

QDateTime WeatherProvider::parseObsTime(const QJsonObject &metar)
{
    const QJsonValue obs = metar.value(QStringLiteral("obsTime"));
    if (obs.isDouble())
        return QDateTime::fromSecsSinceEpoch(qint64(obs.toDouble()), QTimeZone::UTC);
    QDateTime t = QDateTime::fromString(obs.toString(), Qt::ISODate);
    if (!t.isValid())
        t = QDateTime::fromString(metar.value(QStringLiteral("reportTime")).toString(), Qt::ISODateWithMs);
    return t;   // invalid = unknown: the METAR is then never "fresh"
}

double WeatherProvider::parseVisibilitySm(const QJsonValue &visib)
{
    if (visib.isDouble())
        return visib.toDouble();
    QString s = visib.toString().trimmed();
    if (s.endsWith(QLatin1Char('+')))
        s.chop(1);                 // "10+" = 10 SM or more
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? v : qQNaN();
}

int WeatherProvider::ceilingFromClouds(const QJsonArray &clouds)
{
    int ceiling = -1;
    for (const QJsonValue &layer : clouds) {
        const QJsonObject l = layer.toObject();
        const QString cover = l.value(QStringLiteral("cover")).toString();
        if (cover != QLatin1String("BKN") && cover != QLatin1String("OVC") && cover != QLatin1String("OVX"))
            continue;
        const int base = l.value(QStringLiteral("base")).toInt(-1);
        if (base >= 0 && (ceiling < 0 || base < ceiling))
            ceiling = base;
    }
    return ceiling;
}

/**
 * @brief Look up the nearest ICAO station code for given coordinates.
 *
 * Uses the NOAA station API with a 50 km radius.  Blocks the calling
 * thread with a QEventLoop (max 3 seconds) since this is typically
 * called during initialization before the event loop is running.
 */
QString WeatherProvider::lookupIcao(double latitude, double longitude, QString *errorMessage)
{
    QNetworkAccessManager nam;
    QUrl url(QStringLiteral("https://aviationweather.gov/api/data/station"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lat"), QString::number(latitude, 'f', 6));
    query.addQueryItem(QStringLiteral("lon"), QString::number(longitude, 'f', 6));
    query.addQueryItem(QStringLiteral("radius"), QStringLiteral("50"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setTransferTimeout(2500);

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) *errorMessage = reply->errorString();
        reply->deleteLater();
        return {};
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid station response");
        return {};
    }

    QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No stations found near coordinates");
        return {};
    }

    return arr.first().toObject().value(QStringLiteral("icaoId")).toString();
}
