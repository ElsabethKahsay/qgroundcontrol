/**
 * @file PowerModel.h
 * @brief Energy consumption prediction from flight history and default airframe profiles.
 *
 * Provides per-airframe Wh/km defaults (MultiRotor, FixedWing, VTOL, etc.) and
 * supports per-vehicle calibration using actual flight data. The calibrated model
 * uses linear regression on payload vs. consumption, falling back to defaults
 * when insufficient flight data is available.
 *
 * Used by MissionEnergyCheck to determine whether the battery has enough charge
 * for a planned mission distance.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPair>

class DatabaseManager;

/// A single data point from a past flight: payload weight vs. measured energy consumption.
struct PowerCalibrationPoint {
    double payloadKg;    ///< Total payload weight for the flight (kg)
    double whPerKm;      ///< Measured energy consumption (Wh per km)
};

/// Result of an energy estimate calculation.
struct PowerEstimate {
    double whPerKm;          ///< Energy consumption rate (Wh/km) — calibrated or default
    double rangeKm;          ///< Estimated max range on full battery (km)
    double flightTimeMin;    ///< Estimated max flight time (minutes)
    QString sourceLabel;     ///< Human-readable description of the model source
    int dataPointCount;      ///< Number of flight sessions used for calibration (0 = defaults only)
    bool isCalibrated;       ///< True if a per-vehicle calibrated model was used
};

/**
 * Energy consumption model for preflight range/flight-time estimation.
 *
 * Estimates are computed per-vehicle when flight history is available (stored
 * in DatabaseManager), or fall back to generic airframe defaults. The model
 * also accounts for payload weight via a calibrated linear relationship.
 */
class PowerModel : public QObject {
    Q_OBJECT
public:
    explicit PowerModel(QObject *parent = nullptr);

    // Default Wh/km per airframe type (no payload)
    Q_INVOKABLE static double defaultWhPerKm(const QString &airframeType);
    Q_INVOKABLE static double defaultHoverWhPerMin(const QString &airframeType);

    // Compute estimate as QVariantMap for QML consumption: {whPerKm, rangeKm, flightTimeMin, sourceLabel, dataPointCount, isCalibrated}
    Q_INVOKABLE QVariantMap estimateToMap(const QString &deviceUid, double payloadKg,
                                           double batteryCapacityWh,
                                           double missionDistanceKm = -1.0,
                                           const QString &airframeHint = QString()) const;

    // Set calibration data for a vehicle
    void setCalibrationPoints(const QString &deviceUid, const QVector<PowerCalibrationPoint> &points);

    // Get estimate for given payload. deviceUid is used to look up calibration.
    PowerEstimate estimate(const QString &deviceUid, double payloadKg, double batteryCapacityWh,
                           double missionDistanceKm = -1.0,
                           const QString &airframeHint = QString()) const;

    // Minimum data points before using calibrated model (from DB check_config or default 5)
    static int minCalibrationPoints();

signals:
    void calibrationUpdated(const QString &deviceUid);

private:
    /// Per-vehicle linear regression model: whPerKm = intercept + slope * payloadKg
    struct VehicleCalibration {
        QString deviceUid;
        QVector<PowerCalibrationPoint> points;
        double slope = 0.0;     ///< Wh/km per kg of payload
        double intercept = 0.0; ///< Base Wh/km at zero payload
        bool fitted = false;    ///< Whether the linear model has been computed
    };

    /// Fit a linear regression model to the calibration points.
    void fitLinear(VehicleCalibration &cal) const;
    /// Find the calibration record for a device, creating one if it doesn't exist.
    VehicleCalibration *findOrCreate(const QString &deviceUid);

    QVector<VehicleCalibration> m_calibrations; ///< In-memory calibration data per vehicle
};
