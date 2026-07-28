/**
 * @file PowerModel.cpp
 * @brief Energy consumption prediction from flight history and airframe defaults.
 *
 * Provides per-airframe Wh/km defaults and supports per-vehicle calibration
 * using linear regression on payload vs. consumption data. Used by
 * MissionEnergyCheck to estimate whether the battery has sufficient charge
 * for a planned mission distance.
 */

#include "PowerModel.h"

#include <QDebug>
#include <QtMath>
#include <QVariantMap>

#include "DatabaseManager.h"

PowerModel::PowerModel(QObject *parent)
    : QObject(parent)
{
}

// Minimum number of flight sessions required before using a calibrated model.
// Configurable via check_config database, defaults to 5.
int PowerModel::minCalibrationPoints()
{
    QString val = DatabaseManager::instance().getCheckConfig("power_model", "min_calibration_points");
    if (!val.isEmpty()) { bool ok; int v = val.toInt(&ok); if (ok) return v; }
    return 5;
}

// Default cruise energy consumption (Wh/km at zero payload) per airframe type.
// Values are approximate and based on typical small UAS profiles.
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

// Default hover energy consumption (Wh/min) per airframe type.
// Used to account for takeoff/landing hover time in range estimates.
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
    if (points.size() >= minCalibrationPoints()) {
        fitLinear(*cal);
    } else {
        cal->fitted = false;
    }
    emit calibrationUpdated(deviceUid);
}

// Simple linear regression: whPerKm = intercept + slope * payloadKg.
// Requires at least 2 data points. Uses standard least-squares formulas.
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

// Compute a power estimate for a given vehicle, payload, and battery capacity.
//
// Model selection priority:
//   1. DB-calibrated model (if enough flight sessions exist)
//   2. In-memory calibrated model (if setCalibrationPoints was called)
//   3. Default airframe values with a rough +5% per kg payload surcharge
//
// Adds 2 minutes of hover time for takeoff/landing. Assumes 30 km/h avg ground speed.
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

    // Query DB for calibrated data (only if deviceUid is non-empty)
    if (!deviceUid.isEmpty()) {
        auto dbModel = DatabaseManager::instance().getCalibratedPowerModel(deviceUid, minCalibrationPoints());
        if (dbModel.isCalibrated) {
            whPerKm = dbModel.whPerKm;
            dataPts = dbModel.dataPointCount;
            calibrated = true;
            qDebug() << "PowerModel: using calibrated values from DB (" << dataPts << "sessions)";
        } else {
            dataPts = dbModel.dataPointCount;
        }
    }

    // If not DB-calibrated, try in-memory calibration data
    if (!calibrated && !deviceUid.isEmpty()) {
        for (const auto &cal : m_calibrations) {
            if (cal.deviceUid == deviceUid) {
                if (dataPts == 0)
                    dataPts = cal.points.size();
                if (cal.fitted && dataPts >= minCalibrationPoints()) {
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

    // Full battery capacity — safety margin applied by caller (MissionEnergyCheck)
    double usableWh = batteryCapacityWh;

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
        int remaining = minCalibrationPoints() - dataPts;
        src = QStringLiteral("Using defaults \u2014 %1 flight(s) remaining to calibrate")
                  .arg(remaining);
    } else {
        src = QStringLiteral("Using defaults \u2014 %1 flights remaining to calibrate")
                  .arg(minCalibrationPoints());
    }

    return {whPerKm, rangeKm, flightTimeMin, src, dataPts, calibrated};
}

// Convenience wrapper: returns estimate() as a QVariantMap for direct QML consumption.
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
