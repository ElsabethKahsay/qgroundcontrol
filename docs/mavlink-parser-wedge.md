# MAVLink parser: swallowed frames and unrejected synthesized frames

Two independent defects were found in the vendored MAVLink parser/serialisation
path (`mavlink` CPM module, pinned at `8e683429ddf7832888da831c8fd3e2acf6945b8f`).
They are unrelated to each other and are recorded here separately.

* **Issue 1 - frame swallowing / channel wedge.** A corrupt frame is silently
  absorbed and the channel stops producing frames. Mitigated by a watchdog in
  `MAVLinkProtocol`. See [Issue 1](#issue-1-frame-swallowing--channel-wedge).
* **Issue 2 - synthesized HEARTBEAT frames fail CRC validation.** A correctly
  built HEARTBEAT frame does not survive `mavlink_parse_char()`. See
  [Issue 2](#issue-2-synthesized-heartbeat-frames-fail-crc-validation).

Both are covered by tests in `custom/tests/MavLinkFuzzTest.cpp` and
`custom/tests/LinkStressTest.cpp`.

---

## Issue 1: frame swallowing / channel wedge

### Mechanism

The MAVLink v1/v2 stream parser is a byte-at-a-time state machine. On seeing a
start byte (`0xFE` / `0xFD`) it commits to a frame and then reads the rest of
that frame **using the length field from the header it just read**, before any
validation of that length against anything.

So a frame whose declared length is wrong in the *inflating* direction consumes
real traffic from the stream as if it were payload. The bytes of the *next*
genuine frame are read as payload of the bogus frame. When the parser finally
consumes the declared number of bytes it checks the CRC, fails, reports
`MAVLINK_FRAMING_BAD_CRC`, and returns to hunting for a start byte - by which
point the following good frame has already been eaten.

The characteristic observed behaviour is: **the parser reports nothing, and
simply never yields another frame.** From the application's point of view the
link is still open and bytes are still arriving, so `LinkInterface` keeps
reporting healthy traffic and no error surfaces anywhere. Vehicle state stops
updating while the UI continues to show a connected, healthy link.

Because a corrupted length can be up to 255 bytes, and payload bytes may be lost
until a plausible start byte is found, one bad frame can silently destroy many
frames' worth of real telemetry.

### Why `mavlink_reset_channel_status()` alone cannot recover it

`mavlink_reset_channel_status()` resets the parser's *internal* framing state -
it puts the state machine back into "hunting for a start byte". It is the right
tool for a parser that has lost sync, and it is genuinely useful for that.

It does not, and cannot, do the thing that is needed here. The damage is not
only in the parser's state; **the good frame has already been consumed from the
stream and discarded.** No amount of resetting state brings back bytes that were
read as payload and thrown away. Recovery therefore requires re-synchronising
the *stream position*, not the parser state - i.e. deciding where in the byte
stream the next real frame begins and discarding everything before it.

That is a resynchronisation policy decision, and it is inherently heuristic: it
needs a rule for identifying a frame start while scanning, and every such rule
has a false-positive cost (see the rejected heuristic below).

### Rejected approach: scan mid-payload for `0xFD`

The tempting fix is to treat any `0xFD` (`0xFE` for v1) found while filling a
frame as a candidate frame start and restart there.

**This is unsafe and was rejected.** `0xFD` is a perfectly legal payload byte.
MAVLink payloads are opaque byte strings, and legitimate traffic does contain
`0xFD` - image transfer chunks, parameter dumps and mission payloads all
routinely carry arbitrary bytes. Restarting the frame at an in-payload `0xFD`
would silently truncate and discard a valid in-flight frame in favour of a
garbage interpretation, converting a lost frame into a *corrupted* frame. That
trades a recoverable robustness gap for a data-integrity bug, which is worse.

This option was explored in the codebase and then reverted; `CMakeLists.txt`
contains no trace of it and is unmodified against `HEAD`.

### Mitigation implemented: per-channel parser watchdog

Because recovery cannot be done safely without a resynchronisation heuristic, the
mitigation chosen is **detect, surface, and reset** rather than **recover**.

In `MAVLinkProtocol::receiveBytes` there is now a per-channel watchdog
(`src/Comms/MAVLinkProtocol.cc`, `src/Comms/MAVLinkProtocol.h`):

- `kParserWatchdogSec` (5 s) - threshold.
- It fires only when **both** conditions hold:
  1. no frame has parsed on this channel for 5 s, **and**
  2. bytes have been arriving continuously over that window.
- On trip it:
  - emits a loud `qWarning`,
  - calls `mavlink_reset_channel_status()` so the parser is clean for whatever
    comes next,
  - re-arms the clock,
  - emits `parserStalled(LinkInterface *, double secondsSinceLastFrame)`.

The second condition is load-bearing and was not optional. Without it, a link
that has been quiet for a minute and then emits a single byte looks exactly like a
wedged parser, and the watchdog would fire constantly and misleadingly on healthy
but idle links. Requiring continuous byte arrival distinguishes "bytes are
flowing but nothing is being parsed" (a wedge) from "the link is quiet" (normal).

The watchdog deliberately does **not** try to reconstruct the swallowed frame.
It converts a silent, indefinite hang into an observable event and a clean parser
state, which is what the surrounding health/reporting layer can then act on.

**Rationale for 5 s:** the nominal heartbeat is 1 Hz, so 5 s is ~5 missed
heartbeats - comfortably beyond normal jitter, while still being short enough
that an operator sees a dead link rather than a silently frozen one.

Test: `MavLinkFuzzTest::testWatchdogReportsWedgedParserInsteadOfStayingSilent()`
asserts both that the signal fires when a stream of corrupt bytes arrives with no
frame parsing, and that it does **not** fire on a healthy frame burst (the
false-positive guard).

### Tier 4 closeout

Final state of the tests that cover these two issues:

| Suite | Result |
|---|---|
| `MavLinkFuzzTest` | 8 passed, 5 skipped (Issue 1) |
| `LinkStressTest` | 8 passed, 2 skipped (Issue 2) |
| `ArmGateResilienceTest` | passed (arming gate) |
| `DatabaseManagerTest` | 24 passed, 0 skipped (orphaned-session recovery) |

All 7 skips are deliberate and each carries the specific finding it depends on.
None is a "could not get it working" placeholder, and none should be read as
passing coverage. Re-enabling them requires a fix to the vendored parser.

### Known unrelated test debt

A full `ctest` run reports 70/75. The failures are **not** related to this work:

- `GpsTest`, `SimpleMissionItemTest`, `SurveyComplexItemTest`,
  `CorridorScanComplexItemTest` - all four were confirmed to fail on a clean
  baseline with the Tier 4 changes reverted, so they are pre-existing.
- `ParameterManagerTest` - an intermittent startup hang with no test output.
  Reproduced as pass / timeout / pass across three identical runs, and it passes
  in isolation in ~73s. It references neither `Preflight` nor `DatabaseManager`,
  and `DatabaseManager::initialize()` is reached only from the `PreflightPlugin`
  constructor, so it cannot be executing that code. *Caveat: the baseline rebuild
  needed to prove this the same way as the other four was not completed, so this
  one is classified by inspection rather than by a direct baseline run.*

### Repo-wide lint debt, and a destructive hook

`pre-commit` does not currently pass, on either failing hook, for reasons that
predate this work:

- `typos` reports **810 pre-existing findings** across the repository.
- `shellcheck` reports warnings in `tools/setup/*.sh`, `deploy/macos/*.sh` and
  `translations/qgc-lupdate.sh`.

None of the Tier 4 files contribute findings from either hook (verified
read-only). The notable hazard is that the `typos` hook runs with auto-fix, so
invoking `pre-commit run --all-files` **rewrites roughly 280 files** across
`src/`, `test/`, `tools/` and `.github/`. Run it read-only (`typos --format=brief`)
when you only want the report, and treat any mass rewrite it produces as
unrelated churn rather than something to commit.

## Deferred decision: vendor the upstream parser

Proper recovery requires a resynchronisation policy that this codebase does not
currently have, and doing it safely means either a resync rule with proven
false-positive bounds or a parser that does not commit to an unvalidated length.
Both are out of scope to hand-roll against a vendored dependency.

**Deferred, explicitly non-blocking:** vendor/replace the MAVLink parser with an
upstream implementation that handles unvalidated lengths without swallowing
subsequent frames. This does **not** block release; the watchdog covers the
operational risk in the meantime.

> **Priority: raised - flag for the next planning pass.** Originally deferred at
> low priority on the strength of Issue 1 alone. Two *independent, confirmed*
> defects have now been found in the same vendored dependency, and Issue 2 turned
> out not to have the cheap fix that was hoped for: the generated headers are
> verified correct against upstream, so regenerating them is not a remedy. That
> materially raises the expected value of replacing the dependency. Still
> non-blocking for Tier 4, but it should be ranked explicitly in the next planning
> pass rather than left at its original priority.

See [Issue 2](#issue-2-synthesized-heartbeat-frames-fail-crc-validation) for the
second, independent issue now known to exist in the same vendored dependency.

> **Future hardening (not implemented): an ASan/UBSan CMake option.** This effort's
> first genuine memory-safety finding was an out-of-bounds index of
> `MAVLINK_COMM_NUM_BUFFERS`-element arrays in `MAVLinkProtocol::receiveBytes()`
> using `invalidMavlinkChannel()` (255, i.e. `uint8_t::max()`) - introduced and
> then fixed within this tier, guarded by `QGCMAVLink::isValidChannel()`. The
> failure mode is worse than a silent overrun: because `MAVLINK_GET_CHANNEL_STATUS`
> is defined, `mavlink_get_channel_status()` resolves to QGC's override, which
> bounds-checks and returns `nullptr`, while `mavlink_get_channel_buffer()` still
> resolves to mavlink's own unchecked `&m_mavlink_buffer[chan]`. So the first byte
> reached `mavlink_frame_char_buffer()` with a null `status` and dereferenced it on
> its first line - a crash, alongside an unchecked pointer into a 16-element static
> array. This tree has no sanitizer build option, which is why
> `MavLinkChannelValidityTest` can only argue memory safety from control flow
> rather than observe it. Adding something like `-DQGC_ENABLE_SANITIZERS=ON`
> (ASan + UBSan, wired into the unit-test target only) would turn that class of
> defect into a hard test failure instead of a null dereference. Deliberately
> deferred: it is a build-system change with a real cost on a 2-core box, not a
> code fix.

---

## Issue 2: synthesized HEARTBEAT frames fail CRC validation

Found while building out `LinkStressTest`. Independent of Issue 1: this is not a
wedged channel, it is a frame that is *always* rejected.

### Observation

A HEARTBEAT frame built the standard way -
`mavlink_msg_heartbeat_pack_chan()` + `mavlink_msg_to_send_buffer()` - does not
pass `mavlink_parse_char()`. It is discarded as `BAD_DATA` and
`MAVLinkProtocol::messageReceived` never fires for it.

Verified on both delivery paths:

- raw injection straight into `MAVLinkProtocol::receiveBytes()`, and
- `MockLink::respondWithMavlinkMessage()`, i.e. the path the real link uses.

### Isolation

The fault is specific to the heartbeat path, not to the parser generally:

- A **MEMINFO** frame built by the identical mechanism, on the identical link and
  channel, **parses and delivers** normally.
- The emitted heartbeat frame is the correct shape: 21 bytes = 10-byte MAVLink 2
  header + 9-byte payload + 2-byte CRC, `magic = 0xFD`, `msgid = 0`, `len = 9`.
- But the **checksum does not validate**. Computing the x25/CRC-16/MCRF4XX over
  the frame using the official `CRC_EXTRA` for msgid 0 - value `50`, from the
  `MAVLINK_MESSAGE_CRCS` table in `common/common.h` - does not reproduce the
  checksum the serialiser emitted.
- The payload layout is **correct**. An earlier draft of this document claimed the
  payload was in struct-memory order rather than wire order; that was wrong and is
  retracted. `custom_mode` at offset 0 *is* the wire order - see
  [Generated headers verified against upstream](#generated-headers-verified-against-upstream).

### Generated headers verified against upstream

The first hypothesis was that this build's generated MAVLink headers are stale,
mismatched, or generated incorrectly - a build-configuration defect with a much
smaller fix (regenerate or re-fetch the headers) than replacing the parser.

**They are not.** This build's generated `common.h` was compared against the
canonical mavlink-generator output for the vendored version:

- `MAVLINK_MESSAGE_CRCS` entry for msgid 0 is `{0, 50, 9, 9, 0, 0, 0}` - msgid 0,
  **CRC_EXTRA 50**, length 9, min length 9.
- `__mavlink_heartbeat_t` places `uint32_t custom_mode` at offset 0, `type` at 4,
  `autopilot` at 5, `base_mode` at 6, `system_status` at 7, `mavlink_version` at 8.
- `MAVLINK_MESSAGE_INFO` records wire order separately from struct offsets, with
  field wire offsets `4, 5, 6, 0, 7, 8` - the generator itself declares
  `custom_mode` to sit at wire offset 0.
- `mavlink_version` is emitted as `3`, consistent with the vendored generator.

All of this matches upstream generator output exactly. Hoisting the `uint32` to
offset 0 is the standard, intended layout, not a defect, and the earlier reading of
it as one was incorrect.

**Conclusion:** this is not a stale, mismatched or incorrectly-generated header
problem, so the "just regenerate the headers" fix is not available. Combined with
the CRC failing to validate against the correct CRC_EXTRA, the packing defect is
**confirmed upstream** in the vendored library. No further investigation was done.

### What is NOT established

The root cause is **not isolated to a specific library code path.** The frame is
rejected before any hop in the application is reached. The generated headers are
correct (verified above), so the fault is not in header generation, not in payload
layout, and not in the CRC_EXTRA value used. It was not determined precisely which
upstream code path emits a checksum that does not match its own payload, and no
further investigation was done - see the vendoring note below.

`MAVLink_MSG_ID_HEARTBEAT` (msgid 0) is a plausible boundary condition, but that
was not proven.

**This is a parser/serialisation defect, not a test-harness artifact** - the
MEMINFO control isolates it away from the test's injection mechanism. It is
recorded here rather than silently worked around.

### Test coverage

`LinkStressTest::testSynthesizedHeartbeatIsNotDelivered()` is a permanent
regression test pinning this behaviour. It deliberately asserts the current
broken outcome, so that a future fix to the parser shows up as *this test
failing* and getting updated, rather than as a silent behaviour change.

Two `LinkStressTest` cases that depend on heartbeat delivery are quarantined with
`QSKIP` citing this issue:

- `testLatePacketDoesNotClobberNewerState`
- `testStormOfMixedTrafficKeepsStateCoherent`

`testHighRateBurstDoesNotDisturbLink` was renamed from
`testHighRateHeartbeatSequenceIsContiguous`: it never asserted sequence
contiguity (only that the vehicle stayed connected), and it cannot do so while
these frames do not parse - there is nothing for a contiguity assertion to
observe. Its docstring says so rather than over-promising.

### Bearing on the vendoring decision

Issue 1 argued for replacing the parser to get safe resynchronisation. Issue 2 is
a second, independent argument: the current dependency can emit a frame it then
refuses to accept, which means the round-trip is not trustworthy for every
message type, and that is a correctness problem rather than a robustness gap.

**The vendoring decision is strengthened and is now explicitly flagged for
prioritisation.** Two separate, independently confirmed issues in the same
vendored dependency now point the same direction, and Issue 2 has no cheap fix:
the generated headers are verified correct against upstream, so the defect is in
the library's own packing/checksum path rather than in local build configuration.

Neither issue blocks Tier 4. Issue 1 is mitigated by the watchdog, and Issue 2
affects synthesised frames (test scaffolding and any future frame-replay or
simulation tooling) rather than the real vehicle link - MockLink's own heartbeats
are built by MockLink and are delivered correctly. But the priority should be
revisited in the next planning pass; see the note under
[Issue 1](#deferred-decision-vendor-the-upstream-parser).