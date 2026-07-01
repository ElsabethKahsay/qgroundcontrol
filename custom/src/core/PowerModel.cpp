#include "PowerModel.h"

#include <QDebug>
#include <QtMath>
#include <QVariantMap>

PowerModel::PowerModel(QObject *parent)
    : QObject(parent)
{
}

double PowerModel::defaultWhPerKm(const QString &airframeType)
{
    // Static per-airframe consumption rates (Wh/km at cruise, zero payload)
    if (airframeType == QStringLiteral("MultiRotor"))   return 120.0;
    if (airframeType == QStringLiteral("FixedWing"))    return 40.0;
    if (airframeType == QStringLiteral("VTOL"))         return 80.0;
    if (airframeType == QStringLiteral("Rover"))        return 30.0;
    if (airframeType == QStringLiteral("Sub"))          return 200.0;
    return 100.0; // Generic / Unknown
}

double PowerModel::defaultHoverWhPerMin(const QString &airframeType)
{
    if (airframeType == QStringLiteral("MultiRotor"))   return 8.0;
    if (airframeType == QStringLiteral("FixedWing"))    return 2.0;
    if (airframeType == QStringLiteral("VTOL"))         return 6.0;
    if (airframeType == QStringLiteral("Rover"))        return 0.5;
    if (airframeType == QStringLiteral("Sub"))          return 15.0;
    return 5.0;
}

PowerModel::VehicleCalibration *PowerModel::findOrCreate(const QString &deviceUid)
{
    for (auto &c : m_calibrations) {
        if (c.deviceUid == deviceUid) return &c;
    }
    m_calibrations.append({deviceUid, {}, 0.0, 0.0, false});
    return &m_calibrations.last();
}

void PowerModel::setCalibrationPoints(const QString &deviceUid,
                                       const QVector<PowerCalibrationPoint> &points)
{
    auto *cal = findOrCreate(deviceUid);
    cal->points = points;
    if (points.size() >= kMinCalibrationPoints) {
        fitLinear(*cal);
    } else {
        cal->fitted = false;
    }
    emit calibrationUpdated(deviceUid);
}

void PowerModel::fitLinear(VehicleCalibration &cal) const
{
    // Simple linear regression: whPerKm = intercept + slope * payloadKg
    int n = cal.points.size();
    if (n < 2) { cal.fitted = false; return; }

    double sumX = 0, sumY = 0, sumX2 = 0, sumXY = 0;
    for (const auto &p : cal.points) {
        sumX += p.payloadKg;
        sumY += p.whPerKm;
        sumX2 += p.payloadKg * p.payloadKg;
        sumXY += p.payloadKg * p.whPerKm;
    }
    double denom = n * sumX2 - sumX * sumX;
    if (qAbs(denom) < 1e-9) {
        cal.fitted = false;
        return;
    }
    cal.slope = (n * sumXY - sumX * sumY) / denom;
    cal.intercept = (sumY - cal.slope * sumX) / n;
    cal.fitted = true;
}

PowerEstimate PowerModel::estimate(const QString &deviceUid, double payloadKg,
                                    double batteryCapacityWh,
                                    double missionDistanceKm,
                                    const QString &airframeHint) const
{
    // If missionDistanceKm is provided, use it; otherwise assume 5km default for estimate
    double distance = missionDistanceKm > 0 ? missionDistanceKm : 5.0;

    // Use airframe hint if provided, otherwise default to MultiRotor
    QString airframe = airframeHint.isEmpty() ? QStringLiteral("MultiRotor") : airframeHint;

    // Default model
    double whPerKm = defaultWhPerKm(airframe);
    double whPerMinHover = defaultHoverWhPerMin(airframe);
    int dataPts = 0;
    bool calibrated = false;

    // Try to find calibration for this device (only if deviceUid is non-empty)
    if (!deviceUid.isEmpty()) {
        for (const auto &cal : m_calibrations) {
            if (cal.deviceUid == deviceUid) {
                dataPts = cal.points.size();
                if (cal.fitted && dataPts >= kMinCalibrationPoints) {
                    whPerKm = cal.intercept + cal.slope * payloadKg;
                    calibrated = true;
                }
                break;
            }
        }
    }

    // If not calibrated, apply payload surcharge to default (rough: +5% per kg)
    if (!calibrated) {
        whPerKm = whPerKm * (1.0 + 0.05 * qMax(0.0, payloadKg));
        whPerMinHover = whPerMinHover * (1.0 + 0.05 * qMax(0.0, payloadKg));
    }

    // Estimate: 80% usable battery (safety reserve)
    double usableWh = batteryCapacityWh * 0.80;

    // Cruise consumption
    double cruiseWh = distance * whPerKm;
    // Assume 2 min hover for takeoff/landing
    double hoverWh = 2.0 * whPerMinHover;
    double totalWh = cruiseWh + hoverWh;

    double rangeKm = totalWh > 0 ? (usableWh / totalWh) * distance : 0;
    double avgSpeedKmph = 30.0; // assumed average ground speed
    double flightTimeMin = avgSpeedKmph > 0 ? (rangeKm / avgSpeedKmph) * 60.0 : 0;

    QString src;
    if (calibrated) {
        src = QStringLiteral("Calibrated from %1 flights").arg(dataPts);
    } else if (dataPts > 0) {
        src = QStringLiteral("Default model (%1 flights recorded, need %2)")
                  .arg(dataPts).arg(kMinCalibrationPoints);
    } else {
        src = QStringLiteral("Default model (no flight history yet)");
    }

    return {whPerKm, rangeKm, flightTimeMin, src, dataPts, calibrated};
}

QVariantMap PowerModel::estimateToMap(const QString &deviceUid, double payloadKg,
                                       double batteryCapacityWh,
                                       double missionDistanceKm,
                                       const QString &airframeHint) const
{
    PowerEstimate est = estimate(deviceUid, payloadKg, batteryCapacityWh, missionDistanceKm, airframeHint);
    QVariantMap m;
    m[QStringLiteral("whPerKm")] = est.whPerKm;
    m[QStringLiteral("rangeKm")] = est.rangeKm;
    m[QStringLiteral("flightTimeMin")] = est.flightTimeMin;
    m[QStringLiteral("sourceLabel")] = est.sourceLabel;
    m[QStringLiteral("dataPointCount")] = est.dataPointCount;
    m[QStringLiteral("isCalibrated")] = est.isCalibrated;
    return m;
}
