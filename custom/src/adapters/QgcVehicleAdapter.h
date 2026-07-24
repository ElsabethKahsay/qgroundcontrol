#pragma once

/// @file QgcVehicleAdapter.h
/// @brief QML-friendly wrapper around a QGC Vehicle for command dispatch and
///        MAVLink message subscriptions.
///
/// QgcVehicleAdapter exposes the vehicle's armed state, flight mode, type,
/// and connection status as Q_PROPERTY values. It also provides:
///   - sendCommand(): dispatch MAV_CMD messages with up to 7 parameters.
///   - subscribeToMessage() / unsubscribeFromMessage(): register callbacks
///     for specific MAVLink message IDs, delivered via QMetaObject::invokeMethod.
///
/// The adapter lazily connects to MAVLinkProtocol only when the first message
/// subscription is created, avoiding unnecessary message processing overhead.

#include <QObject>
#include <QGeoCoordinate>
#include <QMap>
#include <QPair>
#include <QByteArray>
#include <QVariantList>

class Vehicle;
class MAVLinkProtocol;
class LinkInterface;

/// @class QgcVehicleAdapter
/// @brief Simplified vehicle interface for QML. Exposes armed state, flight mode,
///        vehicle type, and provides command sending + MAVLink message subscriptions.
class QgcVehicleAdapter : public QObject {
    Q_OBJECT

    /// Whether the vehicle is currently armed.
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)

    /// Current flight mode name (e.g. "Stabilize", "Auto", "Loiter").
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)

    /// Human-readable vehicle type string (e.g. "Quadcopter", "VTOL").
    Q_PROPERTY(QString vehicleTypeName READ vehicleType NOTIFY vehicleTypeChanged)

    /// True when the Vehicle object exists (link is active).
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
public:
    /// Construct with a Vehicle. Pass nullptr for a disconnected adapter.
    explicit QgcVehicleAdapter(Vehicle* vehicle, QObject* parent = nullptr);
    ~QgcVehicleAdapter() override;

    /// Send a MAV_CMD command with up to 7 float parameters.
    /// Returns false if the command ID is not in the allowlist.
    Q_INVOKABLE bool sendCommand(int commandId, const QVariantList& args = {});

    /// Subscribe to a MAVLink message ID. When a matching message arrives,
    /// the receiver's slot (signature: void(int, QByteArray)) will be called.
    Q_INVOKABLE void subscribeToMessage(int msgId, QObject* receiver, const char* slot);

    /// Remove a previously registered subscription.
    Q_INVOKABLE void unsubscribeFromMessage(int msgId, QObject* receiver);

    /// Whether the underlying Vehicle object is non-null.
    Q_INVOKABLE bool isConnected() const;

    /// Human-readable vehicle type string.
    Q_INVOKABLE QString vehicleType() const;

    bool armed() const { return _armed; }
    QString flightMode() const { return _flightMode; }
    QGeoCoordinate coordinate() const { return _coordinate; }

signals:
    /// Emitted when the armed state changes.
    void armedChanged(bool armed);

    /// Emitted when the flight mode changes.
    void flightModeChanged(const QString& flightMode);

    /// Emitted when the vehicle's GPS coordinate updates.
    void coordinateChanged(const QGeoCoordinate& coordinate);

    /// Emitted when the vehicle type changes (e.g. after reconnect).
    void vehicleTypeChanged();

    /// Emitted when the connection state changes.
    void connectionChanged(bool connected);

    /// Emitted for every subscribed MAVLink message, with raw payload bytes.
    void messageReceived(int msgId, const QByteArray& payload, quint8 sysid, quint8 compid);

private slots:
    void _onArmedChanged(bool armed);
    void _onFlightModeChanged(const QString& flightMode);
    void _onCoordinateChanged(const QGeoCoordinate& coord);

private:
    /// Lazily connects to MAVLinkProtocol::messageReceived on first subscription.
    void _lazyConnectMavlink();

    /// Internal handler for incoming MAVLink messages; dispatches to subscribers.
    void _onMavlinkMessage(LinkInterface* link, const void* msg);

    /// Convert a MAV_TYPE integer to a human-readable vehicle type string.
    static QString _vehicleTypeToString(int mavType);

    /// Whitelist of command IDs that sendCommand() is allowed to dispatch.
    static bool _isValidCommand(int commandId);

    Vehicle* _vehicle;               ///< Underlying QGC Vehicle (may be null).
    MAVLinkProtocol* _mavlinkProtocol; ///< Singleton MAVLink protocol handler.
    bool _armed;                     ///< Cached armed state.
    QString _flightMode;             ///< Cached flight mode string.
    QGeoCoordinate _coordinate;      ///< Cached vehicle position.
    int _vehicleTypeInt;             ///< MAV_TYPE integer for the vehicle.

    /// Per-message-ID subscription entry: stores the receiver object and slot name.
    struct Subscription {
        QObject* receiver;       ///< Object that owns the slot method.
        QByteArray slotMethod;   ///< Slot method name for QMetaObject::invokeMethod.
    };

    QMap<int, Subscription> _subscriptions; ///< Active message subscriptions keyed by msgid.
    bool _mavlinkConnected;   ///< True once we've connected to MAVLinkProtocol signals.
};
