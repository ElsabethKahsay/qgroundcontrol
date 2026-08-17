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
                       + QStringLiteral("/weather");
    QDir().mkpath(cacheDir);
    m_cache->setCacheDirectory(cacheDir);
    m_cache->setMaximumCacheSize(50 * 1024 * 1024); // 50 MB
    m_nam.setCache(m_cache);
    s_instance = this;
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
    return describeCode(m_weatherCode);
}

QString WeatherProvider::currentSummary() const
{
    if (m_windSpeed < 0.001 && m_visibilityKm < 0.001 && m_temperature < -50.0)
        return QStringLiteral("Weather not yet fetched");
    return QStringLiteral("Wind %1 kt, Vis %2 km, %3, %4\u00B0C")
        .arg(m_windSpeed / 0.514444, 0, 'f', 0)
        .arg(m_visibilityKm, 0, 'f', 1)
        .arg(weatherDescription())
        .arg(m_temperature, 0, 'f', 1);
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
                       "wind_speed_10m,wind_direction_10m,visibility"));
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

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) {
            m_lastError = QStringLiteral("Invalid JSON response");
            emit loadingChanged();
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject current = root.value(QStringLiteral("current")).toObject();

        m_temperature = current.value(QStringLiteral("temperature_2m")).toDouble();
        m_humidity = current.value(QStringLiteral("relative_humidity_2m")).toDouble();
        m_weatherCode = current.value(QStringLiteral("weather_code")).toInt();
        m_windSpeed = current.value(QStringLiteral("wind_speed_10m")).toDouble();
        m_windDirection = current.value(QStringLiteral("wind_direction_10m")).toDouble();
        m_visibility = current.value(QStringLiteral("visibility")).toDouble();
        m_visibilityKm = m_visibility / 1000.0;
        m_metarTimestamp = QDateTime::currentDateTimeUtc();
        m_lastError.clear();

        emit weatherUpdated();
        emit loadingChanged();
    });
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

    // Timestamp
    if (metar.contains(QStringLiteral("obsTime"))) {
        QString ts = metar.value(QStringLiteral("obsTime")).toString();
        m_metarTimestamp = QDateTime::fromString(ts, Qt::ISODate);
    } else {
        m_metarTimestamp = QDateTime::currentDateTimeUtc();
    }

    // Temperature (Celsius)
    if (metar.contains(QStringLiteral("temp")))
        m_temperature = metar.value(QStringLiteral("temp")).toDouble();

    // Wind
    if (metar.contains(QStringLiteral("wspd")))
        m_windSpeed = metar.value(QStringLiteral("wspd")).toDouble() * 0.514444; // knots -> m/s
    if (metar.contains(QStringLiteral("wdir")))
        m_windDirection = metar.value(QStringLiteral("wdir")).toDouble();
    if (metar.contains(QStringLiteral("gust")))
        m_windGust = metar.value(QStringLiteral("gust")).toDouble() * 0.514444;
    else
        m_windGust = 0.0;

    // Visibility (statute miles -> km)
    if (metar.contains(QStringLiteral("visib"))) {
        double visSm = metar.value(QStringLiteral("visib")).toDouble();
        m_visibility = visSm * 1609.34;
        m_visibilityKm = visSm * 1.60934;
    }

    // Ceiling (feet)
    m_ceilingFt = 0;
    if (metar.contains(QStringLiteral("ceiling"))) {
        QJsonObject ceiling = metar.value(QStringLiteral("ceiling")).toObject();
        m_ceilingFt = ceiling.value(QStringLiteral("base_feet_agl")).toInt();
    }

    // Weather code from flight category
    if (metar.contains(QStringLiteral("flightCategory"))) {
        QString cat = metar.value(QStringLiteral("flightCategory")).toString();
        if (cat == QStringLiteral("VFR"))
            m_weatherCode = 0;
        else if (cat == QStringLiteral("MVFR"))
            m_weatherCode = 2;
        else if (cat == QStringLiteral("IFR"))
            m_weatherCode = 45;
        else if (cat == QStringLiteral("LIFR"))
            m_weatherCode = 95;
    }

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
    m_metarParsed << QStringLiteral("Wind: %1%2kt from %3%4")
        .arg(m_windSpeed / 0.514444, 0, 'f', 0)
        .arg(m_windGust > 0.0 ?
             QStringLiteral(" G%1kt").arg(m_windGust / 0.514444, 0, 'f', 0) :
             QString())
        .arg(m_windDirection, 0, 'f', 0)
        .arg(QChar(0x00B0));
    m_metarParsed << QStringLiteral("Vis: %1 km").arg(m_visibilityKm, 0, 'f', 1);
    m_metarParsed << QStringLiteral("Ceiling: %1 ft AGL").arg(m_ceilingFt);
    m_metarParsed << QStringLiteral("Temp: %1 \u00B0C").arg(m_temperature, 0, 'f', 1);
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
    if (m_windSpeed < 0.001 && m_windDirection < 0.5)
        return {};
    return QStringLiteral("%1 kt from %2\u00B0")
        .arg(m_windSpeed / 0.514444, 0, 'f', 0)
        .arg(m_windDirection, 0, 'f', 0);
}

QString WeatherProvider::ceiling() const
{
    if (m_ceilingFt > 0)
        return QStringLiteral("%1 ft AGL").arg(m_ceilingFt);
    if (!m_metarString.isEmpty())
        return QStringLiteral("CAVOK");
    return {};
}

bool WeatherProvider::windOk() const
{
    return m_windSpeed <= kMaxWindMps;
}

QString WeatherProvider::lastUpdated() const
{
    if (!m_metarTimestamp.isValid())
        return {};
    return m_metarTimestamp.toLocalTime().toString(QStringLiteral("dd MMM yyyy  HH:mm"));
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
