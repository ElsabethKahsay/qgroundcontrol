#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QElapsedTimer>

class TelemetryBridge;

/// Manages per-vehicle profile data: device UID resolution, flight session lifecycle,
/// battery tracking, and GPS/payload metadata.  Listens to TelemetryBridge for
/// connection and arm/disarm events to auto-start and end flight sessions in the database.
class VehicleProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentDeviceUid READ currentDeviceUid NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentVehicleHistoryJson READ currentVehicleHistoryJson NOTIFY currentVehicleChanged)
    Q_PROPERTY(QString currentBatterySerial READ currentBatterySerial NOTIFY currentBatteryChanged)
    Q_PROPERTY(int currentFlightSessionId READ currentFlightSessionId NOTIFY currentVehicleChanged)
    Q_PROPERTY(double currentPayloadWeightKg READ currentPayloadWeightKg WRITE setPayloadWeightKg NOTIFY payloadWeightChanged)
    Q_PROPERTY(QString currentLocationName READ currentLocationName WRITE setLocationName NOTIFY locationNameChanged)
    Q_PROPERTY(double planLatitude READ planLatitude WRITE setPlanLatitude NOTIFY planLatitudeChanged)
    Q_PROPERTY(double planLongitude READ planLongitude WRITE setPlanLongitude NOTIFY planLongitudeChanged)

public:
    explicit VehicleProfileManager(QObject *parent = nullptr);

    /// Connect to a TelemetryBridge to receive connection/arm state and vehicle parameters.
    void setTelemetryBridge(TelemetryBridge *bridge);

    /// Resolve motor count from vehicle parameters (CA_AIRFRAME on PX4,
    /// FRAME_CLASS/FRAME_TYPE on ArduPilot). Falls back to defaultCount.
    static int resolveMotorCount(TelemetryBridge *telemetry, int defaultCount = 4);

    QString currentDeviceUid() const { return m_currentDeviceUid; }
    QString currentVehicleHistoryJson() const { return m_currentVehicleHistoryJson; }
    QString currentBatterySerial() const { return m_batterySerial; }
    int currentFlightSessionId() const { return m_flightSessionId; }

    double currentPayloadWeightKg() const { return m_payloadWeightKg; }
    void setPayloadWeightKg(double kg);

    QString currentLocationName() const { return m_locationName; }
    void setLocationName(const QString &name);

    double planLatitude() const { return m_planLat; }
    void setPlanLatitude(double lat);
    double planLongitude() const { return m_planLon; }
    void setPlanLongitude(double lon);

    /// Record battery serial for cycle tracking and persist to the battery table.
    Q_INVOKABLE void setBatterySerial(const QString &serial, const QString &operatorLabel = {});
    Q_INVOKABLE QString loadVehicleHistory(const QString &deviceUid);
    Q_INVOKABLE QStringList knownVehicles();

    Q_INVOKABLE bool deleteVehicle(const QString &deviceUid);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateVehicleProfile(const QString &deviceUid, const QString &pilotName,
                                           const QString &notes);
    Q_INVOKABLE bool updateVehicleFirmware(const QString &fingerprint, const QString &firmwareVersion);

signals:
    void currentVehicleChanged();
    void currentBatteryChanged();
    void payloadWeightChanged();
    void locationNameChanged();
    void planLatitudeChanged();
    void planLongitudeChanged();
    void vehicleReconnected(const QString &deviceUid, const QString &friendlyName);

private slots:
    void onConnectionChanged();
    void _onArmedChanged(bool armed);

private:
    /// Resolve the vehicle's persistent device UID: tries hardware UID first,
    /// then falls back to composite key or system ID.
    QString resolveDeviceUid();
    QString autopilotTypeString();
    /// Determine airframe type from vehicle parameters, falling back to HEARTBEAT flags.
    QString airframeTypeString();

    TelemetryBridge *m_telemetry = nullptr;
    QString m_currentDeviceUid;
    QString m_currentVehicleHistoryJson;
    QString m_batterySerial;
    int m_flightSessionId = -1;
    double m_payloadWeightKg = 0.0;
    QString m_locationName;
    double m_planLat = 0.0;
    double m_planLon = 0.0;
    QElapsedTimer m_sessionTimer;
    bool m_wasArmed = false;
    QElapsedTimer m_armedTimer;          // Tracks arm duration to filter out short test-arms.
    double m_armBatteryPct = -1.0;       // Battery % at arm time, used to compute energy used.
};
