#include "AutopilotInfoDetector.h"
#include "MAVLinkProtocol.h"

#include <QDebug>

AutopilotInfoDetector::AutopilotInfoDetector(QObject *parent)
    : QObject(parent)
{
}

QString AutopilotInfoDetector::autopilotName() const
{
    switch (m_autopilot) {
    case PX4:       return QStringLiteral("PX4 Pro Autopilot");
    case ArduPilot: return QStringLiteral("ArduPilot / ArduCopter");
    case Generic:   return QStringLiteral("Generic MAVLink Autopilot");
    default:        return QStringLiteral("Unknown Autopilot");
    }
}

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
