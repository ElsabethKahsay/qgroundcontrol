/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "MavLinkChannelValidityTest.h"

#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "MAVLinkLib.h"
#include "MAVLinkProtocol.h"
#include "MockConfiguration.h"
#include "QGCMAVLink.h"

#include <QtCore/QList>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <algorithm>

namespace {

/// A link that is constructed but never handed to LinkManager, so
/// LinkInterface::_mavlinkChannel() is left at invalidMavlinkChannel(). This is
/// exactly the "channel was never allocated" state, reached in production when
/// allocateMavlinkChannel() finds every channel reserved.
class UnallocatedChannelLink : public LinkInterface
{
public:
    explicit UnallocatedChannelLink(SharedLinkConfigurationPtr &config)
        : LinkInterface(config)
    {
    }

    void disconnect() override {}
    bool isConnected() const override { return true; }

private:
    void _writeBytes(const QByteArray &) override {}
    bool _connect() override { return true; }
};

QList<QString> capturedWarnings;

void captureWarningHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg) {
        capturedWarnings.append(message);
    }
}

/// Swaps in a warning collector for the lifetime of the scope, so the test can
/// count the guard's log lines instead of having them scroll past unread.
class WarningCapture
{
public:
    WarningCapture()
    {
        capturedWarnings.clear();
        _previous = qInstallMessageHandler(captureWarningHandler);
    }

    ~WarningCapture() { qInstallMessageHandler(_previous); }

    int countContaining(const QString &needle) const
    {
        return static_cast<int>(std::count_if(capturedWarnings.cbegin(), capturedWarnings.cend(),
                                              [&needle](const QString &message) { return message.contains(needle); }));
    }

private:
    QtMessageHandler _previous = nullptr;
};

/// A well-formed HEARTBEAT frame. If bytes were reaching mavlink_parse_char()
/// this would parse and emit MAVLinkProtocol::messageReceived().
QByteArray heartbeatFrame()
{
    mavlink_message_t message{};
    (void) mavlink_msg_heartbeat_pack(255, 1, &message, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4,
                                      MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 0, MAV_STATE_ACTIVE);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
    return QByteArray(reinterpret_cast<const char *>(buffer), static_cast<int>(length));
}

const char *const guardNeedle = "no valid MAVLink channel";

} // namespace

void MavLinkChannelValidityTest::_testInvalidChannelContract()
{
    // The guard in MAVLinkProtocol::receiveBytes() is a single comparison. This
    // pins the two halves of that comparison, plus the array size it is measured
    // against, so a change to any of them cannot silently reintroduce an
    // out-of-bounds index.
    QVERIFY(MAVLinkProtocol::kParserWatchdogSec > 0);
    QVERIFY(MAVLinkProtocol::kInvalidChannelWarnIntervalSec > 0);

    QCOMPARE(static_cast<int>(MAVLINK_COMM_NUM_BUFFERS), 16);
    QCOMPARE(static_cast<int>(LinkManager::invalidMavlinkChannel()), 255);

    // invalidMavlinkChannel() must lie outside every per-channel array in
    // MAVLinkProtocol, otherwise indexing with it is an out-of-bounds access by
    // definition.
    QVERIFY(LinkManager::invalidMavlinkChannel() >= MAVLINK_COMM_NUM_BUFFERS);

    QVERIFY(!QGCMAVLink::isValidChannel(LinkManager::invalidMavlinkChannel()));
    QVERIFY(!QGCMAVLink::isValidChannel(static_cast<uint8_t>(MAVLINK_COMM_NUM_BUFFERS)));
    QVERIFY(!QGCMAVLink::isValidChannel(255));

    for (uint8_t channel = 0; channel < MAVLINK_COMM_NUM_BUFFERS; channel++) {
        QVERIFY2(QGCMAVLink::isValidChannel(channel), "every allocated channel must be valid");
    }
}

void MavLinkChannelValidityTest::_testBytesDroppedForInvalidChannel()
{
    // A private MAVLinkProtocol instance rather than instance(): this block
    // asserts the warning is emitted exactly once, which only holds while the
    // rate limiter's timestamp is still unset. A fresh object guarantees that
    // regardless of what ran before in this process.
    MAVLinkProtocol protocol;

    QSignalSpy messageSpy(&protocol, &MAVLinkProtocol::messageReceived);
    QSignalSpy stallSpy(&protocol, &MAVLinkProtocol::parserStalled);

    SharedLinkConfigurationPtr config(new MockConfiguration(QStringLiteral("unallocated-channel")));
    UnallocatedChannelLink link(config);

    // The link must really be in the state this test claims to exercise.
    QVERIFY(link.mavlinkChannelIsSet() == false);
    QCOMPARE(static_cast<int>(link.mavlinkChannel()), static_cast<int>(LinkManager::invalidMavlinkChannel()));

    const QByteArray frame = heartbeatFrame();
    QVERIFY(!frame.isEmpty());

    int guardWarnings = 0;
    {
        WarningCapture capture;
        for (int i = 0; i < 500; i++) {
            protocol.receiveBytes(&link, frame);
        }
        guardWarnings = capture.countContaining(guardNeedle);
    }

    // Bytes never reached the parser: a frame this well formed would otherwise
    // have produced one messageReceived per call.
    QCOMPARE(messageSpy.count(), 0);

    // The watchdog never evaluated this channel, so no state was read from or
    // written to _lastByteTime/_lastParsedFrameTime for it.
    QCOMPARE(stallSpy.count(), 0);

    // The guard fired, and fired once for 500 calls rather than once per byte.
    // This is the assertion that actually pins the fix: with the guard removed
    // receiveBytes() returns from the "link gone" path instead and logs nothing,
    // so guardWarnings would be 0 and this comparison would fail.
    QCOMPARE(guardWarnings, 1);
}

// NOTE ON WHAT THIS TEST PROVES AND WHAT IT DOES NOT
//
// Proves:
//   - invalidMavlinkChannel() (255) is outside the 16-element per-channel arrays,
//     and isValidChannel() rejects it while accepting 0..15.
//   - receiveBytes() consults that check and drops the bytes before doing
//     anything else, by observing the guard's own log line.
//   - the guard's warning is rate-limited rather than emitted per byte.
//   - no messageReceived/parserStalled is produced, so the parse and watchdog
//     paths are not entered for an invalid channel.
//
// Does NOT prove:
//   - that no out-of-bounds write occurs. This tree has no ASan/UBSan option, so
//     absence of memory corruption can only be argued from control flow (the
//     guard returns first), not observed. A sanitizer build is the only way to
//     confirm that directly - see the hardening note in
//     docs/mavlink-parser-wedge.md.
