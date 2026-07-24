#include "AutopilotInfoDetector.h"
#include "MAVLinkProtocol.h"

#include <QDebug>

AutopilotInfoDetector::AutopilotInfoDetector(QObject *parent)
    : QObject(parent)
{
}

/** @brief Return a human-readable name for the detected autopilot. */
QString AutopilotInfoDetector::autopilotName() const
{
    switch (m_autopilot) {
    case PX4:       return QStringLiteral("PX4 Pro Autopilot");
    case ArduPilot: return QStringLiteral("ArduPilot / ArduCopter");
    case Generic:   return QStringLiteral("Generic MAVLink Autopilot");
    default:        return QStringLiteral("Unknown Autopilot");
    }
}

/**
 * @brief Process a MAVLink HEARTBEAT to identify the autopilot type.
 *
 * Maps the MAV_AUTOPILOT enum to the internal Autopilot enum.  On
 * detection of PX4 or ArduPilot, triggers parameter map loading.
 * Only emits signals if the detected type actually changed.
 */
void AutopilotInfoDetector::consumeHeartbeat(int mavAutopilotEnum)
{
    Autopilot detected;

    switch (mavAutopilotEnum) {
    case MAV_AUTOPILOT_PX4:
        detected = PX4;
        break;
    case MAV_AUTOPILOT_ARDUPILOTMEGA:
        detected = ArduPilot;
        break;
    case MAV_AUTOPILOT_GENERIC:
    default:
        detected = Generic;
        break;
    }

    if (detected == m_autopilot)
        return;

    m_autopilot = detected;
    emit autopilotChanged();
    emit autopilotDetected(detected, autopilotName());

    if (detected == PX4 || detected == ArduPilot) {
        loadParamMap();
    } else {
        emit genericModeActivated();
    }
}

/** @brief Invoke the registered parameter map loader for the detected autopilot. */
void AutopilotInfoDetector::loadParamMap()
{
    switch (m_autopilot) {
    case PX4:
        if (m_px4Loader)
            m_px4Loader();
        emit parameterMapLoaded(PX4);
        break;
    case ArduPilot:
        if (m_arduLoader)
            m_arduLoader();
        emit parameterMapLoaded(ArduPilot);
        break;
    default:
        break;
    }
}
