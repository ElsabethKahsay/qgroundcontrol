#pragma once

#include <QObject>
#include <QGeoCoordinate>
#include <QMap>
#include <QPair>
#include <QByteArray>
#include <QVariantList>

class Vehicle;
class MAVLinkProtocol;
class LinkInterface;

class QgcVehicleAdapter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(QString vehicleTypeName READ vehicleType NOTIFY vehicleTypeChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
public:
    explicit QgcVehicleAdapter(Vehicle* vehicle, QObject* parent = nullptr);
    ~QgcVehicleAdapter() override;

    Q_INVOKABLE bool sendCommand(int commandId, const QVariantList& args = {});
    Q_INVOKABLE void subscribeToMessage(int msgId, QObject* receiver, const char* slot);
    Q_INVOKABLE void unsubscribeFromMessage(int msgId, QObject* receiver);
    Q_INVOKABLE bool isConnected() const;
    Q_INVOKABLE QString vehicleType() const;

    bool armed() const { return _armed; }
    QString flightMode() const { return _flightMode; }
    QGeoCoordinate coordinate() const { return _coordinate; }

signals:
    void armedChanged(bool armed);
    void flightModeChanged(const QString& flightMode);
    void coordinateChanged(const QGeoCoordinate& coordinate);
    void vehicleTypeChanged();
    void connectionChanged(bool connected);
    void messageReceived(int msgId, const QByteArray& payload, quint8 sysid, quint8 compid);

private slots:
    void _onArmedChanged(bool armed);
    void _onFlightModeChanged(const QString& flightMode);
    void _onCoordinateChanged(const QGeoCoordinate& coord);

private:
    void _lazyConnectMavlink();
    void _onMavlinkMessage(LinkInterface* link, const void* msg);
    static QString _vehicleTypeToString(int mavType);
    static bool _isValidCommand(int commandId);

    Vehicle* _vehicle;
    MAVLinkProtocol* _mavlinkProtocol;
    bool _armed;
    QString _flightMode;
    QGeoCoordinate _coordinate;
    int _vehicleTypeInt;

    struct Subscription {
        QObject* receiver;
        QByteArray slotMethod;
    };
    QMap<int, Subscription> _subscriptions;
    bool _mavlinkConnected;
};
