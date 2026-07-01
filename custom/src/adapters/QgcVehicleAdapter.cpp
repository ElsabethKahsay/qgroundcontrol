#include "QgcVehicleAdapter.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>

#include "Comms/MAVLinkProtocol.h"
#include "mavlink_types.h"
#include "QGCApplication.h"
#include "QGCMAVLink.h"
#include "Vehicle/Vehicle.h"

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

    _armed = _vehicle->armed();
    _flightMode = _vehicle->flightMode();
    _coordinate = _vehicle->coordinate();
    _vehicleTypeInt = static_cast<int>(_vehicle->vehicleType());

    connect(_vehicle, &Vehicle::armedChanged, this, &QgcVehicleAdapter::_onArmedChanged);
    connect(_vehicle, &Vehicle::flightModeChanged, this, &QgcVehicleAdapter::_onFlightModeChanged);
    connect(_vehicle, &Vehicle::coordinateChanged, this, &QgcVehicleAdapter::_onCoordinateChanged);
}

QgcVehicleAdapter::~QgcVehicleAdapter()
{
    if (_mavlinkProtocol && _mavlinkConnected) {
        disconnect(_mavlinkProtocol, nullptr, this, nullptr);
    }
}

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

void QgcVehicleAdapter::unsubscribeFromMessage(int msgId, QObject* receiver)
{
    auto it = _subscriptions.find(msgId);
    if (it != _subscriptions.end() && it->receiver == receiver) {
        _subscriptions.erase(it);
        qCDebug(qgcVehicleAdapterLog) << "Unsubscribed from msgId" << msgId;
    }
}

bool QgcVehicleAdapter::isConnected() const
{
    if (!_vehicle) {
        return false;
    }
    return true;
}

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

void QgcVehicleAdapter::_onMavlinkMessage(LinkInterface* link, const void* msg)
{
    Q_UNUSED(link)
    if (!msg) {
        return;
    }
    const mavlink_message_t* mavMsg = static_cast<const mavlink_message_t*>(msg);

    auto it = _subscriptions.find(mavMsg->msgid);
    if (it == _subscriptions.end()) {
        return;
    }

    if (!it->receiver) {
        _subscriptions.erase(it);
        return;
    }

    QByteArray payload(reinterpret_cast<const char*>(mavMsg->payload64),
                       static_cast<int>(mavMsg->len));
    emit messageReceived(static_cast<int>(mavMsg->msgid), payload,
                         mavMsg->sysid, mavMsg->compid);

    QMetaObject::invokeMethod(it->receiver, it->slotMethod.constData(),
                              Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(mavMsg->msgid)),
                              Q_ARG(QByteArray, payload));
}

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

bool QgcVehicleAdapter::_isValidCommand(int commandId)
{
    return commandId == MAV_CMD_COMPONENT_ARM_DISARM ||
           commandId == MAV_CMD_DO_MOTOR_TEST ||
           commandId == MAV_CMD_DO_REPOSITION ||
           commandId == MAV_CMD_DO_CHANGE_SPEED;
}
