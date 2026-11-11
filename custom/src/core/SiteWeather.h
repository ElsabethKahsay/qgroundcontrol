// Weather at the flying site, for the mission estimate.
//
// Two sources are combined:
// - The forecast at the site (Open-Meteo), hour by hour for the next hours:
//   temperature and humidity at 2 m, wind at 10, 80 and 120 m, gusts at 10 m,
//   and the height of the ground the forecast is made for.
// - The METAR of an airport (by default Addis Ababa Bole, HAAB): measured
//   temperature, dew point and wind at the airport, and the airport's height.
//
// What is worked out for a flight height above the site:
// - Wind: the forecast hour nearest the take-off time, between its 10, 80
//   and 120 m values. Wind grows with the logarithm of height, so the speed is
//   interpolated in ln(height); the direction is interpolated as a vector.
//   Below 10 m the 10 m value is used, above 120 m the 120 m value. The
//   forecast has gusts only at 10 m; the gust at height keeps the same excess
//   over the mean wind (gust - wind at 10 m), never less than the 10 m gust.
// - Temperature: the forecast temperature, moved from the forecast's ground
//   height to the site's height and then up to the flight height at
//   6.5 °C per 1000 m (the standard atmosphere's lapse rate). Without a
//   forecast, the METAR temperature is moved the same way from the airport's
//   height.
// - Without a forecast, the METAR wind is used and the confidence is Low.
//
// The confidence drops, with a reason, when the forecast is older than
// 3 hours, when it was made for a point more than 10 km from the site, or when
// only the METAR is there. Nothing is ever filled in with a made-up value: a
// value nobody provided is NaN.
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

namespace SiteWeather {

constexpr double kLapseCPerM = 0.0065;        ///< standard atmosphere, °C per metre
constexpr double kOldAfterS = 3 * 3600;       ///< forecast older than this lowers confidence
constexpr double kFarAfterM = 10000.0;        ///< forecast point further than this from the site
constexpr double kMetarOldAfterS = 3600;      ///< METAR older than this is named as a reason
constexpr int kForecastHours = 12;            ///< hours asked for
constexpr double kHeights[3] = {10.0, 80.0, 120.0};

inline double nan() { return std::numeric_limits<double>::quiet_NaN(); }

enum Confidence { None = 0, Low = 1, Medium = 2, High = 3 };

inline QString confidenceName(int c)
{
    switch (c) {
    case High: return QStringLiteral("High");
    case Medium: return QStringLiteral("Medium");
    case Low: return QStringLiteral("Low");
    default: return QStringLiteral("None");
    }
}

/// One forecast hour.
struct Hour {
    qint64 utc = 0;                ///< start of the hour, Unix seconds
    double tempC = nan();          ///< at 2 m
    double humidityPct = nan();    ///< at 2 m
    double windMs[3] = {nan(), nan(), nan()};      ///< at 10, 80, 120 m
    double windFromDeg[3] = {nan(), nan(), nan()}; ///< direction the wind comes from
    double gust10Ms = nan();
};

struct Forecast {
    bool ok = false;
    double lat = nan();
    double lon = nan();
    double groundAmslM = nan();    ///< height of the ground the forecast is made for
    qint64 issuedUtc = 0;          ///< the forecast's "current" time (when the data is from)
    QVector<Hour> hours;
};

/// The airport observation.
struct Station {
    bool ok = false;
    QString id;
    double lat = nan();
    double lon = nan();
    double elevM = nan();
    double tempC = nan();
    double dewC = nan();
    double windMs = nan();
    double windFromDeg = nan();    ///< NaN for variable wind
    double gustMs = nan();         ///< 0 when the METAR reports no gusts
    qint64 obsUtc = 0;
};

/// The weather for one flight height at the site.
struct Conditions {
    bool windKnown = false;
    double windMs = nan();
    double windFromDeg = nan();
    double gustMs = nan();
    double tempC = nan();
    double humidityPct = nan();
    QString windSource;            ///< "forecast" or "METAR HAAB"
    QString tempSource;
    qint64 forecastHourUtc = 0;    ///< the forecast hour used; 0 when none
    double forecastAgeS = nan();
    double metarAgeS = nan();
    double metarWindMs = nan();    ///< shown next to the forecast as a check
    double metarGustMs = nan();
    int confidence = None;
    QStringList reasons;
};

/// True when the gust (the mean wind when no gust is known) is above the
/// operator's limit (anything above 12 m/s on the 22-inch copter): shown in
/// red. False when either is unknown.
inline bool gustAbove(const Conditions &c, double limitMs)
{
    const double g = std::isfinite(c.gustMs) ? c.gustMs : c.windMs;
    return std::isfinite(g) && std::isfinite(limitMs) && limitMs > 0.0 && g > limitMs;
}

inline double distanceM(double lat1, double lon1, double lat2, double lon2)
{
    if (!std::isfinite(lat1) || !std::isfinite(lon1) || !std::isfinite(lat2) || !std::isfinite(lon2))
        return nan();
    constexpr double R = 6371000.0;
    constexpr double d2r = 3.14159265358979323846 / 180.0;
    const double dLat = (lat2 - lat1) * d2r;
    const double dLon = (lon2 - lon1) * d2r;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                     + std::cos(lat1 * d2r) * std::cos(lat2 * d2r) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

/// Temperature moved from one height to another at the standard lapse rate.
inline double lapseTempC(double tempC, double fromAmslM, double toAmslM)
{
    if (!std::isfinite(tempC) || !std::isfinite(fromAmslM) || !std::isfinite(toAmslM))
        return nan();
    return tempC - kLapseCPerM * (toAmslM - fromAmslM);
}

/// Wind at a height above the ground from the 10, 80 and 120 m values.
/// speed and direction are NaN when the hour has no wind.
inline void windAtHeight(const Hour &h, double heightAglM, double *speedMs, double *fromDeg, double *gustMs)
{
    *speedMs = *fromDeg = *gustMs = nan();
    // Pairs of known heights; the 10 m value is needed for anything.
    if (!std::isfinite(h.windMs[0]))
        return;
    const double z = std::isfinite(heightAglM) ? std::clamp(heightAglM, kHeights[0], kHeights[2]) : kHeights[0];
    int lo = 0;
    int hi = 0;
    for (int i = 1; i < 3; ++i) {
        if (!std::isfinite(h.windMs[i]))
            break;
        if (kHeights[i] <= z)
            lo = i;
        hi = i;
        if (kHeights[i] >= z)
            break;
    }
    if (kHeights[hi] < z)
        lo = hi;   // missing higher values: the highest known one
    const double f = (hi == lo) ? 0.0
                                : (std::log(z) - std::log(kHeights[lo])) / (std::log(kHeights[hi]) - std::log(kHeights[lo]));
    *speedMs = h.windMs[lo] + f * (h.windMs[hi] - h.windMs[lo]);

    constexpr double d2r = 3.14159265358979323846 / 180.0;
    if (std::isfinite(h.windFromDeg[lo]) && std::isfinite(h.windFromDeg[hi])) {
        const double x = (1 - f) * std::sin(h.windFromDeg[lo] * d2r) + f * std::sin(h.windFromDeg[hi] * d2r);
        const double y = (1 - f) * std::cos(h.windFromDeg[lo] * d2r) + f * std::cos(h.windFromDeg[hi] * d2r);
        if (std::hypot(x, y) > 1e-9) {
            double d = std::atan2(x, y) / d2r;
            *fromDeg = d < 0 ? d + 360.0 : d;
        }
    }
    if (std::isfinite(h.gust10Ms))
        *gustMs = std::max(h.gust10Ms, *speedMs + (h.gust10Ms - h.windMs[0]));
}

/// A time from Open-Meteo: Unix seconds, or an ISO local time with the
/// response's UTC offset.
inline qint64 openMeteoTime(const QJsonValue &v, int utcOffsetS)
{
    if (v.isDouble())
        return qint64(v.toDouble());
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODate);
    if (!t.isValid())
        return 0;
    t.setTimeZone(QTimeZone::UTC);
    return t.toSecsSinceEpoch() - utcOffsetS;
}

/// Reads an Open-Meteo forecast with an "hourly" block. A response without
/// hours is not ok; missing values in an hour are NaN.
inline Forecast parseOpenMeteo(const QByteArray &json, QString *error = nullptr)
{
    Forecast f;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Forecast is not JSON");
        return f;
    }
    const QJsonObject o = doc.object();
    const int offset = o.value(QStringLiteral("utc_offset_seconds")).toInt(0);
    auto num = [](const QJsonValue &v) { return v.isDouble() ? v.toDouble() : nan(); };
    f.lat = num(o.value(QStringLiteral("latitude")));
    f.lon = num(o.value(QStringLiteral("longitude")));
    f.groundAmslM = num(o.value(QStringLiteral("elevation")));
    f.issuedUtc = openMeteoTime(o.value(QStringLiteral("current")).toObject().value(QStringLiteral("time")), offset);

    const QJsonObject hourly = o.value(QStringLiteral("hourly")).toObject();
    const QJsonArray times = hourly.value(QStringLiteral("time")).toArray();
    auto col = [&hourly](const char *key) { return hourly.value(QLatin1String(key)).toArray(); };
    const QJsonArray t2 = col("temperature_2m");
    const QJsonArray rh = col("relative_humidity_2m");
    const QJsonArray w[3] = {col("wind_speed_10m"), col("wind_speed_80m"), col("wind_speed_120m")};
    const QJsonArray d[3] = {col("wind_direction_10m"), col("wind_direction_80m"), col("wind_direction_120m")};
    const QJsonArray g = col("wind_gusts_10m");
    for (int i = 0; i < times.size(); ++i) {
        Hour h;
        h.utc = openMeteoTime(times.at(i), offset);
        if (h.utc == 0)
            continue;
        h.tempC = num(t2.at(i));
        h.humidityPct = num(rh.at(i));
        for (int k = 0; k < 3; ++k) {
            h.windMs[k] = num(w[k].at(i));
            h.windFromDeg[k] = num(d[k].at(i));
        }
        h.gust10Ms = num(g.at(i));
        f.hours.append(h);
    }
    if (f.hours.isEmpty()) {
        if (error) *error = QStringLiteral("Forecast has no hours");
        return f;
    }
    if (f.issuedUtc == 0)
        f.issuedUtc = f.hours.first().utc;   // no "current" block: the first hour
    f.ok = true;
    return f;
}

/// Reads the first observation of an aviationweather.gov METAR JSON array.
/// Wind in knots becomes m/s.
inline Station parseMetar(const QByteArray &json)
{
    Station s;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray() || doc.array().isEmpty())
        return s;
    const QJsonObject m = doc.array().first().toObject();
    auto num = [&m](const char *key) {
        const QJsonValue v = m.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : nan();
    };
    constexpr double kt = 0.514444;
    s.id = m.value(QStringLiteral("icaoId")).toString();
    s.lat = num("lat");
    s.lon = num("lon");
    s.elevM = num("elev");
    s.tempC = num("temp");
    s.dewC = num("dewp");
    s.windMs = num("wspd") * kt;
    s.windFromDeg = num("wdir");
    if (m.contains(QStringLiteral("wgst")))
        s.gustMs = num("wgst") * kt;
    else if (m.contains(QStringLiteral("gust")))
        s.gustMs = num("gust") * kt;
    else
        s.gustMs = std::isfinite(s.windMs) ? 0.0 : nan();
    const QJsonValue obs = m.value(QStringLiteral("obsTime"));
    if (obs.isDouble()) {
        s.obsUtc = qint64(obs.toDouble());
    } else {
        QDateTime t = QDateTime::fromString(obs.toString(), Qt::ISODate);
        s.obsUtc = t.isValid() ? t.toSecsSinceEpoch() : 0;
    }
    s.ok = !s.id.isEmpty();
    return s;
}

/// The forecast hour covering a time: the hour that starts at or before it,
/// within the hour. -1 when the forecast does not cover the time.
inline int hourFor(const Forecast &f, qint64 utc)
{
    for (int i = 0; i < f.hours.size(); ++i) {
        const qint64 start = f.hours.at(i).utc;
        if (utc >= start && utc < start + 3600)
            return i;
    }
    return -1;
}

inline QString hoursText(double s)
{
    return s < 3600 ? QStringLiteral("%1 min").arg(qRound(s / 60.0))
                    : QStringLiteral("%1 h").arg(s / 3600.0, 0, 'f', 1);
}

/// The weather at a flight height above the site at a given time.
/// siteAmslM may be NaN (site height unknown); the temperature is then the
/// source's own, not moved, and a reason says so.
inline Conditions conditionsAt(const Forecast &f, const Station &st, qint64 nowUtc, double siteLat, double siteLon,
                               double siteAmslM, double heightAglM)
{
    Conditions c;
    const double h = std::isfinite(heightAglM) ? std::max(0.0, heightAglM) : 0.0;
    const bool siteHeightKnown = std::isfinite(siteAmslM);
    const double flightAmsl = siteHeightKnown ? siteAmslM + h : nan();

    if (st.ok) {
        c.metarWindMs = st.windMs;
        c.metarGustMs = st.gustMs;
        if (st.obsUtc > 0)
            c.metarAgeS = double(nowUtc - st.obsUtc);
    }

    const int idx = f.ok ? hourFor(f, nowUtc) : -1;
    if (f.ok && idx < 0)
        c.reasons << QStringLiteral("The saved forecast does not cover this hour");

    if (idx >= 0) {
        const Hour &hr = f.hours.at(idx);
        windAtHeight(hr, h, &c.windMs, &c.windFromDeg, &c.gustMs);
        c.windKnown = std::isfinite(c.windMs);
        c.windSource = QStringLiteral("forecast");
        c.forecastHourUtc = hr.utc;
        c.forecastAgeS = double(nowUtc - f.issuedUtc);
        c.humidityPct = hr.humidityPct;
        if (std::isfinite(hr.tempC)) {
            if (siteHeightKnown && std::isfinite(f.groundAmslM)) {
                c.tempC = lapseTempC(hr.tempC, f.groundAmslM, flightAmsl);
            } else {
                c.tempC = lapseTempC(hr.tempC, 0.0, h);
                if (!siteHeightKnown)
                    c.reasons << QStringLiteral("Site height not known: temperature not corrected for it");
            }
            c.tempSource = QStringLiteral("forecast");
        }
        c.confidence = High;
        if (c.forecastAgeS > kOldAfterS) {
            c.reasons << QStringLiteral("Old weather: forecast %1 old").arg(hoursText(c.forecastAgeS));
            c.confidence = Medium;
        }
        const double away = distanceM(f.lat, f.lon, siteLat, siteLon);
        if (std::isfinite(away) && away > kFarAfterM) {
            c.reasons << QStringLiteral("Forecast is for a point %1 km from the site").arg(qRound(away / 1000.0));
            c.confidence = Medium;
        }
    }

    // Without a usable forecast the METAR stands in, at Low confidence.
    if (!c.windKnown && st.ok && std::isfinite(st.windMs)) {
        c.windKnown = true;
        c.windMs = st.windMs;
        c.windFromDeg = st.windFromDeg;
        c.gustMs = st.gustMs;
        c.windSource = QStringLiteral("METAR %1").arg(st.id);
        c.confidence = Low;
        c.reasons << QStringLiteral("No forecast for the site: wind from the %1 METAR").arg(st.id);
    }
    if (!std::isfinite(c.tempC) && st.ok && std::isfinite(st.tempC)) {
        if (siteHeightKnown && std::isfinite(st.elevM)) {
            c.tempC = lapseTempC(st.tempC, st.elevM, flightAmsl);
        } else {
            c.tempC = st.tempC;
            c.reasons << QStringLiteral("Site height not known: %1 temperature not corrected for it").arg(st.id);
        }
        c.tempSource = QStringLiteral("METAR %1").arg(st.id);
        if (!std::isfinite(c.humidityPct) && std::isfinite(st.dewC)) {
            const double e = 6.112 * std::exp(17.67 * st.dewC / (st.dewC + 243.5));
            const double es = 6.112 * std::exp(17.67 * st.tempC / (st.tempC + 243.5));
            c.humidityPct = std::clamp(e / es * 100.0, 0.0, 100.0);
        }
    }
    if (c.windSource.startsWith(QStringLiteral("METAR")) && std::isfinite(c.metarAgeS) && c.metarAgeS > kMetarOldAfterS)
        c.reasons << QStringLiteral("Old weather: METAR %1 old").arg(hoursText(c.metarAgeS));
    if (!c.windKnown) {
        c.confidence = None;
        c.reasons << QStringLiteral("No weather for the site");
    }
    return c;
}

} // namespace SiteWeather
