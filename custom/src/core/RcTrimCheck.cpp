#include "RcTrimCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

RcTrimCheck::RcTrimCheck(TelemetryBridge *telemetry,
                         int centerTolerance,
                         QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.trim"),
                    QStringLiteral("RC Trim"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
    , m_centerTolerance(centerTolerance)
{
    m_telemetry = telemetry;
}

void RcTrimCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariantList channels = m_telemetry->property("rcChannelValues").toList();

    if (channels.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channel data"));
        return;
    }

    // Check first 4 channels (throttle may be at 0, so skip channel 3)
    // Roll (ch1), Pitch (ch2), Yaw (ch4) should be near 1500
    int center = 1500;
    QStringList offChannels;

    if (channels.size() >= 1) {
        int v = static_cast<int>(channels[0].toUInt());
        if (qAbs(v - center) > m_centerTolerance)
            offChannels << QStringLiteral("Roll=%1").arg(v);
    }
    if (channels.size() >= 2) {
        int v = static_cast<int>(channels[1].toUInt());
        if (qAbs(v - center) > m_centerTolerance)
            offChannels << QStringLiteral("Pitch=%1").arg(v);
    }
    if (channels.size() >= 4) {
        int v = static_cast<int>(channels[3].toUInt());
        if (qAbs(v - center) > m_centerTolerance)
            offChannels << QStringLiteral("Yaw=%1").arg(v);
    }

    if (offChannels.isEmpty()) {
        setStatus(CheckStatus::Passed, QStringLiteral("Sticks centered"));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Trim off: %1").arg(offChannels.join(QStringLiteral(", "))));
    }
}
