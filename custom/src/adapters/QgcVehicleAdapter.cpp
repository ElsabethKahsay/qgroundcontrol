/// @file QgcVehicleAdapter.cpp
/// @brief Implementation of the QgcVehicleAdapter vehicle wrapper.

#include <QDebug>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>

#include "Comms/MAVLinkProtocol.h"
#include "mavlink_types.h"
#include "QGCApplication.h"
#include "QGCMAVLink.h"
#include "Vehicle/Vehicle.h"

/// Logging category for QgcVehicleAdapter debug/warning output.
Q_LOGGING_CATEGORY(qgcVehicleAdapterLog, "qgc.vehicle.adapter")

QgcVehicleAdapter::QgcVehicleAdapter(Vehicle* vehicle, QObject* parent)
    : QObject(parent)
    , _vehicle(vehicle)
    , _mavlinkProtocol(nullptr)
    , _armed(false)
    , _flightMode()
    , _vehicleTypeInt(0)
    , _mavlinkConnected(false)
{
    if (!_vehicle) {
        qCWarning(qgcVehicleAdapterLog) << "QgcVehicleAdapter constructed with null Vehicle";
        return;
    }

    // Seed cached state from the current Vehicle.
    _armed = _vehicle->armed();
    _flightMode = _vehicle->flightMode();
    _coordinate = _vehicle->coordinate();
    _vehicleTypeInt = static_cast<int>(_vehicle->vehicleType());

    // Forward Vehicle state-change signals.
    connect(_vehicle, &Vehicle::armedChanged, this, &QgcVehicleAdapter::_onArmedChanged);
    connect(_vehicle, &Vehicle::flightModeChanged, this, &QgcVehicleAdapter::_onFlightModeChanged);
    connect(_vehicle, &Vehicle::coordinateChanged, this, &QgcVehicleAdapter::_onCoordinateChanged);
}

QgcVehicleAdapter::~QgcVehicleAdapter()
{
    // Disconnect from MAVLinkProtocol if we connected during the lifetime of this adapter.
    if (_mavlinkProtocol && _mavlinkConnected) {
        disconnect(_mavlinkProtocol, nullptr, this, nullptr);
    }
}

/// Send a MAV_CMD command. Only commands in the allowlist are dispatched.
/// The args list maps positionally to the 7 MAV_CMD param fields (param1-param7).
bool QgcVehicleAdapter::sendCommand(int commandId, const QVariantList& args)
{
    if (!_vehicle) {
        qCWarning(qgcVehicleAdapterLog) << "sendCommand: no vehicle";
        return false;
    }

    if (!_isValidCommand(commandId)) {
        qCWarning(qgcVehicleAdapterLog) << "sendCommand: unknown command id" << commandId;
        return false;
    }

    auto mavCmd = static_cast<MAV_CMD>(commandId);

    // Pack up to 7 parameters from the QVariant list, defaulting to 0.
    float params[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    int count = qMin(args.size(), 7);
    for (int i = 0; i < count; ++i) {
        params[i] = static_cast<float>(args[i].toDouble());
    }

    _vehicle->sendMavCommand(MAV_COMP_ID_AUTOPILOT1, mavCmd, false,
                             params[0], params[1], params[2], params[3],
                             params[4], params[5], params[6]);
    qCDebug(qgcVehicleAdapterLog) << "sendCommand" << commandId << "params:" << params[0] << params[1] << params[2] << params[3] << params[4] << params[5] << params[6];
    return true;
}

/// Register a callback for a specific MAVLink message ID. The slot must have
/// the signature: void slot(int msgId, QByteArray payload).
/// Lazily connects to MAVLinkProtocol on the first subscription.
void QgcVehicleAdapter::subscribeToMessage(int msgId, QObject* receiver, const char* slot)
{
    if (!receiver || !slot) {
        qCWarning(qgcVehicleAdapterLog) << "subscribeToMessage: invalid receiver or slot";
        return;
    }

    _subscriptions[msgId] = { receiver, QByteArray(slot) };
    _lazyConnectMavlink();
    qCDebug(qgcVehicleAdapterLog) << "Subscribed to msgId" << msgId;
}

/// Remove a subscription for a specific message ID and receiver.
void QgcVehicleAdapter::unsubscribeFromMessage(int msgId, QObject* receiver)
{
    auto it = _subscriptions.find(msgId);
    if (it != _subscriptions.end() && it->receiver == receiver) {
        _subscriptions.erase(it);
        qCDebug(qgcVehicleAdapterLog) << "Unsubscribed from msgId" << msgId;
    }
}

/// True when the underlying Vehicle pointer is non-null.
bool QgcVehicleAdapter::isConnected() const
{
    if (!_vehicle) {
        return false;
    }
    return true;
}

/// Human-readable vehicle type string derived from the MAV_TYPE enum.
QString QgcVehicleAdapter::vehicleType() const
{
    if (!_vehicle) {
        return QStringLiteral("Unknown");
    }
    return _vehicleTypeToString(static_cast<int>(_vehicle->vehicleType()));
}

void QgcVehicleAdapter::_onArmedChanged(bool armed)
{
    _armed = armed;
    emit armedChanged(armed);
}

void QgcVehicleAdapter::_onFlightModeChanged(const QString& flightMode)
{
    _flightMode = flightMode;
    emit flightModeChanged(flightMode);
}

void QgcVehicleAdapter::_onCoordinateChanged(const QGeoCoordinate& coord)
{
    _coordinate = coord;
    emit coordinateChanged(coord);
}

/// Internal handler for incoming MAVLink messages. Looks up the subscription
/// for the message ID and dispatches the raw payload to the registered receiver
/// via QMetaObject::invokeMethod (queued connection).
void QgcVehicleAdapter::_onMavlinkMessage(LinkInterface* link, const void* msg)
{
    Q_UNUSED(link)
    if (!msg) {
        return;
    }
    const mavlink_message_t* mavMsg = static_cast<const mavlink_message_t*>(msg);

    // Only process messages that have an active subscription.
    auto it = _subscriptions.find(mavMsg->msgid);
    if (it == _subscriptions.end()) {
        return;
    }

    // Guard against stale subscriptions where the receiver was destroyed.
    if (!it->receiver) {
        _subscriptions.erase(it);
        return;
    }

    // Extract the raw payload bytes from the MAVLink message.
    QByteArray payload(reinterpret_cast<const char*>(mavMsg->payload64),
                       static_cast<int>(mavMsg->len));

    // Emit the general signal so any listener can observe the message.
    emit messageReceived(static_cast<int>(mavMsg->msgid), payload,
                         mavMsg->sysid, mavMsg->compid);

    // Dispatch to the specific subscriber's slot with the same signature.
    QMetaObject::invokeMethod(it->receiver, it->slotMethod.constData(),
                              Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(mavMsg->msgid)),
                              Q_ARG(QByteArray, payload));
}

/// Lazily connect to the global MAVLinkProtocol singleton. Only connects once,
/// on the first call to subscribeToMessage().
void QgcVehicleAdapter::_lazyConnectMavlink()
{
    if (_mavlinkConnected || !_vehicle) {
        return;
    }

    _mavlinkProtocol = MAVLinkProtocol::instance();
    if (!_mavlinkProtocol) {
        qCWarning(qgcVehicleAdapterLog) << "Cannot get MAVLinkProtocol from toolbox";
        return;
    }

    connect(_mavlinkProtocol, &MAVLinkProtocol::messageReceived,
            this, [this](LinkInterface* link, mavlink_message_t message) {
                _onMavlinkMessage(link, &message);
            });
    _mavlinkConnected = true;
}

/// Convert a MAV_TYPE enum value to a short, human-readable vehicle type string.
/// Covers common airframe types: quad, fixed-wing, VTOL variants, helicopter,
/// ground rover, submarine, boat, and airship.
QString QgcVehicleAdapter::_vehicleTypeToString(int mavType)
{
    switch (mavType) {
    case MAV_TYPE_QUADROTOR:
        return QStringLiteral("Quadcopter");
    case MAV_TYPE_FIXED_WING:
        return QStringLiteral("FixedWing");
    case MAV_TYPE_VTOL_TILTROTOR:
    case MAV_TYPE_VTOL_TAILSITTER:
    case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
    case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
    case MAV_TYPE_VTOL_FIXEDROTOR:
    case MAV_TYPE_VTOL_TILTWING:
    case MAV_TYPE_VTOL_RESERVED5:
        return QStringLiteral("VTOL");
    case MAV_TYPE_HELICOPTER:
        return QStringLiteral("Helicopter");
    case MAV_TYPE_GROUND_ROVER:
        return QStringLiteral("Rover");
    case MAV_TYPE_SUBMARINE:
        return QStringLiteral("Submarine");
    case MAV_TYPE_SURFACE_BOAT:
        return QStringLiteral("Boat");
    case MAV_TYPE_AIRSHIP:
        return QStringLiteral("Airship");
    default:
        return QStringLiteral("Unknown");
    }
}

/// Whitelist of command IDs that sendCommand() is allowed to dispatch.
/// This prevents accidental sending of dangerous commands (e.g. emergency actions).
bool QgcVehicleAdapter::_isValidCommand(int commandId)
{
    return commandId == MAV_CMD_COMPONENT_ARM_DISARM ||
           commandId == MAV_CMD_DO_MOTOR_TEST ||
           commandId == MAV_CMD_DO_REPOSITION ||
           commandId == MAV_CMD_DO_CHANGE_SPEED ||
           commandId == MAV_CMD_SET_MESSAGE_INTERVAL;
}
