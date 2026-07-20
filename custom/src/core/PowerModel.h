#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPair>

class DatabaseManager;

struct PowerCalibrationPoint {
    double payloadKg;
    double whPerKm;
};

struct PowerEstimate {
    double whPerKm;
    double rangeKm;
    double flightTimeMin;
    QString sourceLabel;   // e.g. "Default (MultiRotor)" or "Calibrated from 8 flights"
    int dataPointCount;
    bool isCalibrated;
};

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
    struct VehicleCalibration {
        QString deviceUid;
        QVector<PowerCalibrationPoint> points;
        double slope = 0.0;   // Wh/km per kg payload
        double intercept = 0.0; // base Wh/km (zero payload)
        bool fitted = false;
    };

    void fitLinear(VehicleCalibration &cal) const;
    VehicleCalibration *findOrCreate(const QString &deviceUid);

    QVector<VehicleCalibration> m_calibrations;
};
