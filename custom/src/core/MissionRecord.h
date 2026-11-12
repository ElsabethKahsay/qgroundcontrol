// The mission estimate against the real flight (step 9 of the mission
// estimate): what was predicted at take-off next to what happened, kept with
// the motor-table flight record so later flights show how accurate the
// estimate is (open item 38).
//
// A flight's record:
// - the result given at take-off, and its predicted total time and landing
//   charge (likely and pessimistic);
// - each mission item: the predicted time from take-off and charge on
//   arrival, and the real ones when it was reached (the autopilot moved on);
// - the real flight time and the charge left at the disarm;
// - the payload releases (which, how much, when, how).
//
// Accuracy over flights: the landing charge's error in points (real -
// likely) and the flight time's error in % of the prediction, averaged as
// absolute values; and how many flights landed at or above the pessimistic
// prediction (the pessimistic case is meant to be on the safe side).
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <cmath>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace MissionRecord {

struct Item {
    int seq = -1;
    double predTimeS = -1.0;
    double predSocLikely = -1.0;
    double predSocPess = -1.0;
    double realTimeS = -1.0;      ///< -1: not reached
    double realSoc = -1.0;
};

struct Record {
    QString resultAtTakeoff;
    double predTotalS = -1.0;
    double predLandLikely = -1.0;
    double predLandPess = -1.0;
    double realTotalS = -1.0;
    double realLandSoc = -1.0;
    QVector<Item> items;
    QVariantList drops;           ///< {index, kg, timeS, how}
    bool valid() const { return predTotalS > 0.0 && realTotalS > 0.0; }
};

inline QVariantMap toMap(const Record &r)
{
    QVariantMap m;
    m[QStringLiteral("resultAtTakeoff")] = r.resultAtTakeoff;
    m[QStringLiteral("predTotalS")] = r.predTotalS;
    m[QStringLiteral("predLandLikely")] = r.predLandLikely;
    m[QStringLiteral("predLandPess")] = r.predLandPess;
    m[QStringLiteral("realTotalS")] = r.realTotalS;
    m[QStringLiteral("realLandSoc")] = r.realLandSoc;
    QVariantList items;
    for (const Item &i : r.items) {
        QVariantMap im;
        im[QStringLiteral("seq")] = i.seq;
        im[QStringLiteral("predTimeS")] = i.predTimeS;
        im[QStringLiteral("predSocLikely")] = i.predSocLikely;
        im[QStringLiteral("predSocPess")] = i.predSocPess;
        im[QStringLiteral("realTimeS")] = i.realTimeS;
        im[QStringLiteral("realSoc")] = i.realSoc;
        items.append(im);
    }
    m[QStringLiteral("items")] = items;
    m[QStringLiteral("drops")] = r.drops;
    return m;
}

inline Record fromMap(const QVariantMap &m)
{
    Record r;
    auto num = [&m](const char *k) { return m.value(QLatin1String(k), -1.0).toDouble(); };
    r.resultAtTakeoff = m.value(QStringLiteral("resultAtTakeoff")).toString();
    r.predTotalS = num("predTotalS");
    r.predLandLikely = num("predLandLikely");
    r.predLandPess = num("predLandPess");
    r.realTotalS = num("realTotalS");
    r.realLandSoc = num("realLandSoc");
    for (const QVariant &v : m.value(QStringLiteral("items")).toList()) {
        const QVariantMap im = v.toMap();
        Item i;
        i.seq = im.value(QStringLiteral("seq"), -1).toInt();
        i.predTimeS = im.value(QStringLiteral("predTimeS"), -1.0).toDouble();
        i.predSocLikely = im.value(QStringLiteral("predSocLikely"), -1.0).toDouble();
        i.predSocPess = im.value(QStringLiteral("predSocPess"), -1.0).toDouble();
        i.realTimeS = im.value(QStringLiteral("realTimeS"), -1.0).toDouble();
        i.realSoc = im.value(QStringLiteral("realSoc"), -1.0).toDouble();
        r.items.append(i);
    }
    r.drops = m.value(QStringLiteral("drops")).toList();
    return r;
}

inline QString toJson(const Record &r)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(toMap(r))).toJson(QJsonDocument::Compact));
}

inline Record fromJson(const QString &json)
{
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
    return d.isObject() ? fromMap(d.object().toVariantMap()) : Record();
}

inline QString clock(double s)
{
    if (!(s >= 0.0))
        return QStringLiteral("--");
    const int t = qRound(s);
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

/// Landing charge error in points: real - predicted (likely).
inline double landSocErrorPts(const Record &r)
{
    return r.valid() && r.realLandSoc >= 0.0 && r.predLandLikely >= 0.0 ? r.realLandSoc - r.predLandLikely : std::nan("");
}

/// Flight time error in % of the predicted total.
inline double timeErrorPct(const Record &r)
{
    return r.valid() ? (r.realTotalS - r.predTotalS) / r.predTotalS * 100.0 : std::nan("");
}

/// One line for the flights list, e.g. "Mission: landed after 2:58 (plan 2:59,
/// -1 %) with 84 % (plan 85 %, -1 pt; pessimistic 83 %). Comfortable at take-off.
/// 4 of 5 items reached."
inline QString summary(const Record &r)
{
    if (!r.valid())
        return QString();
    int reached = 0;
    for (const Item &i : r.items)
        reached += i.realTimeS >= 0.0 ? 1 : 0;
    const double ePts = landSocErrorPts(r);
    QString charge = r.realLandSoc >= 0.0
                         ? QStringLiteral(" with %1 % (plan %2 %, %3%4 pt; pessimistic %5 %)")
                               .arg(qRound(r.realLandSoc)).arg(qRound(r.predLandLikely))
                               .arg(ePts >= 0.0 ? QStringLiteral("+") : QString()).arg(ePts, 0, 'f', 0)
                               .arg(qRound(r.predLandPess))
                         : QString();
    return QStringLiteral("Mission: landed after %1 (plan %2, %3%4 %)%5. %6 at take-off. %7 of %8 items reached.")
        .arg(clock(r.realTotalS), clock(r.predTotalS))
        .arg(timeErrorPct(r) >= 0.0 ? QStringLiteral("+") : QString()).arg(timeErrorPct(r), 0, 'f', 0)
        .arg(charge, r.resultAtTakeoff.isEmpty() ? QStringLiteral("No result") : r.resultAtTakeoff)
        .arg(reached).arg(r.items.size());
}

struct Accuracy {
    int flights = 0;
    double meanAbsSocPts = std::nan("");
    double meanAbsTimePct = std::nan("");
    int atOrAbovePessimistic = 0;  ///< landed with at least the pessimistic charge
};

inline Accuracy accuracy(const QVector<Record> &records)
{
    Accuracy a;
    double sumSoc = 0.0, sumTime = 0.0;
    int nSoc = 0;
    for (const Record &r : records) {
        if (!r.valid())
            continue;
        ++a.flights;
        sumTime += std::fabs(timeErrorPct(r));
        const double e = landSocErrorPts(r);
        if (std::isfinite(e)) {
            sumSoc += std::fabs(e);
            ++nSoc;
            if (r.realLandSoc >= r.predLandPess)
                ++a.atOrAbovePessimistic;
        }
    }
    if (a.flights > 0)
        a.meanAbsTimePct = sumTime / a.flights;
    if (nSoc > 0)
        a.meanAbsSocPts = sumSoc / nSoc;
    return a;
}

} // namespace MissionRecord
