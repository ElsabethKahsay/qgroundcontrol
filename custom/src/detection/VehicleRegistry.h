#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>

class Vehicle;

class VehicleRegistry : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("QML.Element", "singleton")
    Q_PROPERTY(int currentVehicleId READ currentVehicleId NOTIFY currentVehicleChanged)
    Q_PROPERTY(bool isKnownVehicle READ isKnownVehicle NOTIFY knownVehicleChanged)
    Q_PROPERTY(QString vehicleName READ vehicleName NOTIFY knownVehicleChanged)
    Q_PROPERTY(QString currentFingerprint READ currentFingerprint NOTIFY currentVehicleChanged)

public:
    static VehicleRegistry *instance();

    explicit VehicleRegistry(QObject *parent = nullptr);

    int currentVehicleId() const { return m_currentVehicleId; }
    bool isKnownVehicle() const { return m_isKnownVehicle; }
    QString vehicleName() const { return m_vehicleName; }
    QString currentFingerprint() const { return m_currentFingerprint; }

    static QString generateFingerprint(quint64 uid, int autopilotType, const QString &boardVersion);

    static QString autopilotTypeString(int autopilotType);
    static QString vehicleTypeString(int vehicleType);

signals:
    void currentVehicleChanged(int vehicleId);
    void knownVehicleChanged();
    void newVehicleRegistered(int vehicleId);
    void knownVehicleConnected(int vehicleId);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _onActiveVehicleChanged(Vehicle *vehicle);

private:
    void _extractVehicleInfo(Vehicle *vehicle);

    int m_currentVehicleId = 0;
    bool m_isKnownVehicle = false;
    QString m_vehicleName;
    QString m_currentFingerprint;
    int m_currentCompid = 0;
    int m_currentAutopilotType = 0;
    int m_currentVehicleType = 0;
    QString m_currentFirmwareVersion;
    quint64 m_currentUid = 0;
    QString m_currentBoardVersion;
};
