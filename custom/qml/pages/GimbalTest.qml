import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QGroundControl
import com.uav.preflight 1.0
import cpts 1.0

Page {
    id: root
    anchors.fill: parent

    signal backRequested

    // Transparent background so the 3% margin around pink outer box shows the parent background
    background: Item {}

    // MAV_RESULT enum labels for gimbal command acks.
    // MAVLink MAV_RESULT codes reported in gimbal command acks (0=Accepted … 7=Only)
    readonly property var _ackLabels: ({
            0: "Accepted",
            1: "Temp Rejected",
            2: "Denied",
            3: "Unsupported",
            4: "Failed",
            5: "In Progress",
            6: "Cancelled",
            7: "Only"
        })

    property string _lastCmd: ""
    property string _lastAck: ""
    // FORK PATCH: the tests stay disabled when the bridge has no gimbal
    // command, rather than throwing on click.
    readonly property bool _gimbalCmdSupported: typeof TelemetryProvider !== "undefined" && TelemetryProvider
                                                && typeof TelemetryProvider.sendMountControl === "function"

    // ── Item 18: per-axis gimbal tests, passed or failed on MEASURED attitude ──
    // Each axis: command a target inside its MNT limits, pass only if the
    // gimbal reports within ±3° of it within 3 s, then command centre and
    // confirm it returns within ±3° within 3 s. No gimbal, no feedback or a
    // rejected command is a fail; an axis with no range is skipped.
    readonly property real _axisToleranceDeg: 3
    readonly property int  _axisTimeoutMs: 3000
    readonly property var  _axisDefaults: ({ pitch: -30, roll: 20, yaw: 30 })
    // {pitch|roll|yaw: {state: "pass"|"fail"|"skip"|"running", text}}
    property var    _axisResults: ({})
    property var    _axisQueue: []
    property string _testAxis: ""
    property real   _testTarget: 0
    property int    _testPhase: 0          // 1 = moving to target, 2 = returning to centre
    property real   _phaseStartMs: 0
    property real   _testStartMs: 0
    readonly property bool axisTestRunning: _testAxis !== ""

    function _param(name) {
        return TelemetryProvider.hasParameter(name) ? TelemetryProvider.parameterValue(name, 0) : NaN
    }
    // [min, max] in degrees: MNT1_* (ArduPilot 4.3+), else the older MNT_ANGMIN/ANGMAX_* in centidegrees.
    function _axisLimits(axis) {
        var n = { pitch: "PITCH", roll: "ROLL", yaw: "YAW" }[axis]
        var lo = _param("MNT1_" + n + "_MIN"), hi = _param("MNT1_" + n + "_MAX")
        if (!isNaN(lo) && !isNaN(hi))
            return [lo, hi]
        var o = { pitch: "TIL", roll: "ROL", yaw: "PAN" }[axis]
        lo = _param("MNT_ANGMIN_" + o); hi = _param("MNT_ANGMAX_" + o)
        if (!isNaN(lo) && !isNaN(hi))
            return [lo / 100, hi / 100]
        return null
    }
    function _measured(axis) {
        return axis === "pitch" ? TelemetryProvider.gimbalPitch
             : axis === "roll"  ? TelemetryProvider.gimbalRoll : TelemetryProvider.gimbalYaw
    }
    function _command(axis, angle) {
        TelemetryProvider.sendMountControl(axis === "pitch" ? angle : 0, axis === "roll" ? angle : 0,
                                           axis === "yaw" ? angle : 0, 2)
    }
    function _setAxis(axis, state, text) {
        var r = Object.assign({}, _axisResults)
        r[axis] = { state: state, text: text }
        _axisResults = r
    }
    function _finishAxis(state, text) {
        var axis = _testAxis
        _setAxis(axis, state, text)
        if (typeof Database !== "undefined" && Database)
            Database.logHardwareTestStep(FlightSession.currentFlightId, "gimbal." + axis, 0,
                                         Math.round(_testTarget), Math.round(_measured(axis)),
                                         -_axisToleranceDeg, _axisToleranceDeg, false, state,
                                         state === "pass" ? "" : text, "", Date.now() - _testStartMs)
        console.info("GimbalTest:", axis, state, text)
        if (state === "fail" && _testPhase === 1)
            _command(axis, 0)            // do not leave the gimbal deflected
        _testAxis = ""
        _testPhase = 0
        axisTimer.stop()
        _startNextAxis()
    }
    function runAxisTests(axes) {
        if (axisTestRunning)
            return
        _axisQueue = axes.slice()
        _startNextAxis()
    }
    function _startNextAxis() {
        if (_axisQueue.length === 0)
            return
        var q = _axisQueue.slice()
        var axis = q.shift()
        _axisQueue = q
        _testAxis = axis
        _testStartMs = Date.now()
        var mntType = _param("MNT1_TYPE")
        if (mntType === 0) { _finishAxis("fail", qsTr("No gimbal configured (MNT1_TYPE = 0)")); return }
        if (!TelemetryProvider.gimbalDetected) { _finishAxis("fail", qsTr("No gimbal attitude feedback from the vehicle")); return }
        var lim = _axisLimits(axis)
        var lo = lim ? lim[0] + 2 : -45, hi = lim ? lim[1] - 2 : 45
        var clamp = function(v) { return Math.max(lo, Math.min(hi, v)) }
        var target = clamp(_axisDefaults[axis])
        if (Math.abs(target) < 5)
            target = clamp(-_axisDefaults[axis])
        if (Math.abs(target) < 5) {
            _testTarget = 0
            _finishAxis("skip", lim ? qsTr("No range (limits %1° to %2°)").arg(lim[0]).arg(lim[1])
                                    : qsTr("No range"))
            return
        }
        _testTarget = Math.round(target)
        _testPhase = 1
        _phaseStartMs = Date.now()
        _setAxis(axis, "running", qsTr("Moving to %1°…").arg(_testTarget))
        _command(axis, _testTarget)
        root._send(qsTr("Gimbal %1 test: %2°").arg(axis).arg(_testTarget))
        axisTimer.start()
    }

    Timer {
        id: axisTimer
        interval: 100
        repeat: true
        onTriggered: {
            var axis = root._testAxis
            if (axis === "") { stop(); return }
            var m = root._measured(axis)
            var elapsed = Date.now() - root._phaseStartMs
            if (root._testPhase === 1) {
                if (Math.abs(m - root._testTarget) <= root._axisToleranceDeg) {
                    root._testPhase = 2
                    root._phaseStartMs = Date.now()
                    root._setAxis(axis, "running", qsTr("Reached %1°, returning to centre…").arg(m.toFixed(1)))
                    root._command(axis, 0)
                } else if (elapsed > root._axisTimeoutMs) {
                    root._finishAxis("fail", qsTr("Commanded %1°, measured %2° after 3 s").arg(root._testTarget).arg(m.toFixed(1)))
                }
            } else if (root._testPhase === 2) {
                if (Math.abs(m) <= root._axisToleranceDeg)
                    root._finishAxis("pass", qsTr("Reached %1° and returned to centre").arg(root._testTarget))
                else if (elapsed > root._axisTimeoutMs)
                    root._finishAxis("fail", qsTr("Did not return to centre (measured %1°)").arg(m.toFixed(1)))
            }
        }
    }
    property bool _targetFromMap: false

    // PITCH / ROLL / YAW card: the gimbal's measured angles when a gimbal
    // reports, otherwise the UAV's own attitude (ATTITUDE, radians -> degrees;
    // yaw as heading 0-360). Tilt the airframe (or gimbal) and these follow.
    readonly property bool _hasTelemetry: typeof TelemetryProvider !== "undefined" && !!TelemetryProvider
    readonly property bool _attFromGimbal: _hasTelemetry && TelemetryProvider.gimbalDetected
    readonly property real _uavPitchDeg: !_hasTelemetry ? 0 : _attFromGimbal ? TelemetryProvider.gimbalPitch
                                                                             : TelemetryProvider.pitch * 180 / Math.PI
    readonly property real _uavRollDeg:  !_hasTelemetry ? 0 : _attFromGimbal ? TelemetryProvider.gimbalRoll
                                                                             : TelemetryProvider.roll * 180 / Math.PI
    readonly property real _uavYawDeg:   !_hasTelemetry ? 0 : _attFromGimbal ? TelemetryProvider.gimbalYaw
                                                                             : ((TelemetryProvider.yaw * 180 / Math.PI) + 360) % 360

    // Control surface test state (driven by ControlSurfaceTestController)
    readonly property var _surfaces: (typeof ControlSurfaceTestController !== "undefined") ? ControlSurfaceTestController.surfaces : []
    readonly property var _states:   (typeof ControlSurfaceTestController !== "undefined") ? ControlSurfaceTestController.surfaceStates : []
    readonly property bool _armed:   (typeof ControlSurfaceTestController !== "undefined") ? ControlSurfaceTestController.isArmed : false

    // Multirotors have no servos/control surfaces, so the sweep only applies
    // to fixed-wing and VTOL airframes.
    readonly property bool _isMultirotor: {
        var vt = (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager.vehicleType : ""
        return vt === "QUAD" || vt === "HEX" || vt === "OCTA" || vt === "TRI"
    }

    // Loads the fixed-wing/VTOL servo map for the current airframe from ControlSurfaceTestController (C++)
    function _loadSurfaces() {
        if (typeof ControlSurfaceTestController !== "undefined"
                && typeof VehicleProfileManager !== "undefined") {
            ControlSurfaceTestController.loadSurfacesForVehicle(
                VehicleProfileManager.vehicleType, VehicleProfileManager.motorCount)
        }
    }

    Component.onCompleted: {
        _loadSurfaces()
        // Seed the target fields from the saved target (the open flight's, else
        // the connected vehicle's), and point the distance readout at it.
        FlightSession.reloadTargetLocation()
        if (!isNaN(FlightSession.targetLat) && !isNaN(FlightSession.targetLon)) {
            targetLat.text = FlightSession.targetLat.toFixed(6)
            targetLon.text = FlightSession.targetLon.toFixed(6)
            // FlightSession persists the source string; "map center" round-trips back here.
            root._targetFromMap = (FlightSession.targetSource === qsTr("map center"))
            DistanceTracker.setTarget(FlightSession.targetLat, FlightSession.targetLon)
        }
    }
    // FORK PATCH (B2): stopAllSurfaces() force-disarms an armed vehicle, so it
    // never runs while flying (no surface sweep can be running then).
    readonly property var  _vehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool _flying: !!_vehicle && _vehicle.armed && _vehicle.flying
    function _safeStopAllSurfaces() {
        if (typeof ControlSurfaceTestController === "undefined")
            return
        if (root._flying) {
            console.warn("GimbalTest: stopAllSurfaces skipped — vehicle is flying (would force-disarm)")
            return
        }
        ControlSurfaceTestController.stopAllSurfaces()
    }

    // Teardown: halt surface sweeps and stop every timer so nothing fires once the page closes
    Component.onDestruction: {
        root._safeStopAllSurfaces()
        ackTimer.stop()
        axisTimer.stop()
        if (root.axisTestRunning && root._gimbalCmdSupported)
            TelemetryProvider.sendMountControl(0, 0, 0, 2)   // never leave the gimbal deflected
        targetStatusTimer.stop()
    }

    Connections {
        target: (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager : null
        function onVehicleTypeResolved() { _loadSurfaces() }
    }

    function _send(cmd) {
        _lastCmd = cmd;
        _lastAck = "sent...";
        statusToast.text = cmd + " \u2192 sent";
        statusToast.color = Colors.info;
        statusToast.opacity = 1.0;
        ackTimer.restart();
    }

    Timer {
        id: ackTimer
        interval: 4000
        onTriggered: {
            statusToast.opacity = 0.0;
        }
    }

    Connections {
        target: (typeof TelemetryProvider !== "undefined") ? TelemetryProvider : null
        function onGimbalCommandResult(command, result) {
            // 205 = MAV_CMD_DO_MOUNT_CONTROL, 1000 = MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW
            if (command !== 205 && command !== 1000)
                return;
            if (result !== 0 && root.axisTestRunning) {
                root._finishAxis("fail", qsTr("Vehicle rejected the gimbal command (%1)")
                                     .arg(root._ackLabels[result] || result));
            }
            var label = root._ackLabels[result] || ("Unknown(" + result + ")");
            root._lastAck = label;
            statusToast.text = root._lastCmd + " \u2192 " + label;
            statusToast.color = result === 0 ? Colors.success : Colors.warning;
            statusToast.opacity = 1.0;
            ackTimer.restart();
        }
    }

    // ── Pink Outer Box with 3% margin OUTSIDE surrounding it ──
    Rectangle {
        id: pinkOuterBox
        anchors.fill: parent
        anchors.margins: parent.width * 0.02
        radius: Config.radiusLarge
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Colors.surfaceLight
            }
            GradientStop {
                position: 1.0
                color: Colors.surface
            }
        }
        border.color: Colors.border
        border.width: 2

        ScrollView {
            anchors.fill: parent
            anchors.margins: Config.spacingMedium
            clip: true
            contentWidth: parent.width - Config.spacingMedium * 2

            ColumnLayout {
                width: parent.width
                spacing: Config.spacingMedium

                // ── Target Location Card (Top of GimbalTest page) ──
                Rectangle {
                    id: targetLocationCard
                    Layout.fillWidth: true
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1
                    implicitHeight: targetCol.implicitHeight + Config.spacingMedium * 2

                    ColumnLayout {
                        id: targetCol
                        anchors { fill: parent; margins: Config.spacingMedium }
                        spacing: 8

                        Label {
                            text: qsTr("Target Location")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: qsTr("Coordinates:")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textSecondary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            TextField {
                                id: targetLat
                                Layout.preferredWidth: 120
                                placeholderText: qsTr("Latitude")
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                                background: Rectangle {
                                    color: Colors.surfaceLight
                                    radius: Config.radiusSmall
                                    border.color: targetLat.activeFocus ? Colors.accent : Colors.border
                                    border.width: 1
                                }
                                onTextChanged: {
                                    // Manual entry demotes the source tag away from "map center"
                                    root._targetFromMap = false
                                    DistanceTracker.setTarget(
                                        parseFloat(targetLat.text), parseFloat(targetLon.text))
                                }
                            }
                            TextField {
                                id: targetLon
                                Layout.preferredWidth: 120
                                placeholderText: qsTr("Longitude")
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                                background: Rectangle {
                                    color: Colors.surfaceLight
                                    radius: Config.radiusSmall
                                    border.color: targetLon.activeFocus ? Colors.accent : Colors.border
                                    border.width: 1
                                }
                                onTextChanged: {
                                    root._targetFromMap = false
                                    DistanceTracker.setTarget(
                                        parseFloat(targetLat.text), parseFloat(targetLon.text))
                                }
                            }
                            Button {
                                text: qsTr("📍 Use Map Center")
                                font.pixelSize: Config.fontSizeSmall
                                onClicked: {
                                    targetLat.text = QGroundControl.flightMapPosition.latitude.toFixed(6)
                                    targetLon.text = QGroundControl.flightMapPosition.longitude.toFixed(6)
                                    root._targetFromMap = true
                                }
                                ToolTip.text: qsTr("Use the current map center as the target location")
                                ToolTip.visible: hovered
                            }
                            Button {
                                id: targetSubmitBtn
                                text: qsTr("Submit Target")
                                font.pixelSize: Config.fontSizeSmall
                                // Saved on the connected vehicle (and the open flight, if any)
                                enabled: !!root._vehicle || FlightSession.currentFlightId > 0
                                onClicked: {
                                    var lat = parseFloat(targetLat.text)
                                    var lon = parseFloat(targetLon.text)
                                    if (isNaN(lat) || isNaN(lon)) {
                                        targetStatus.show(qsTr("Enter latitude and longitude before saving"), false)
                                        return
                                    }
                                    // Tag must match the literal compared in Component.onCompleted (map center)
                                    var src = root._targetFromMap ? qsTr("map center") : qsTr("manual")
                                    if (FlightSession.saveTargetLocation(lat, lon, src)) {
                                        var detail = src
                                        if (DistanceTracker.hasTarget && DistanceTracker.distanceStr.length > 0)
                                            detail += qsTr(" \u00B7 %1 from vehicle").arg(DistanceTracker.distanceStr)
                                        targetStatus.show(qsTr("Target location saved (%1)").arg(detail), true)
                                    } else {
                                        targetStatus.show(FlightSession.lastTargetError, false)
                                    }
                                }
                                ToolTip.text: qsTr("Save this target location for this vehicle's flights")
                                ToolTip.visible: hovered
                            }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Label {
                                id: targetStatus
                                function show(msg, ok) {
                                    targetStatus._ok = ok
                                    targetStatus.text = msg
                                    if (ok) targetStatusTimer.restart()
                                }
                                property bool _ok: false
                                visible: text.length > 0
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                color: _ok ? Colors.statePass : Colors.error
                                wrapMode: Text.WordWrap
                                Layout.maximumWidth: 350

                                Timer {
                                    id: targetStatusTimer
                                    interval: 4000
                                    onTriggered: targetStatus.text = ""
                                }
                            }

                            Label {
                                visible: DistanceTracker.hasTarget && !isNaN(DistanceTracker.distanceM)
                                text: qsTr("Distance: ") + DistanceTracker.distanceStr
                                      + "  ·  " + qsTr("Bearing: ") + DistanceTracker.bearingStr
                                font.bold: true
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textSecondary
                            }
                        }
                    }
                }

                // ── Combined Warning & Airspace Status Card ──
                Rectangle {
                    id: zoneCard
                    Layout.fillWidth: true
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1
                    implicitHeight: zoneCol.implicitHeight

                    property bool expanded: false

                    // Automatic route-compliance check registered by PreflightManager.
                    readonly property var _zoneCheck: (typeof PreflightManager !== "undefined")
                        ? PreflightManager.checkById("airspace.zone_compliance") : null

                    // Snapshot binding: re-reads the intersecting/unacked lists
                    // whenever the check re-evaluates (status/message NOTIFY).
                    readonly property string snap: _zoneCheck
                        ? (_zoneCheck.status + "|" + _zoneCheck.message) : ""
                    property var _intersecting: []
                    property var _unacked: []

                    function refresh() {
                        var inter = [], un = []
                        if (_zoneCheck && _zoneCheck.intersectingZoneIds !== undefined) {
                            inter = _zoneCheck.intersectingZoneIds()
                            un = _zoneCheck.unacknowledgedZoneIds()
                        }
                        zoneCard._intersecting = inter
                        zoneCard._unacked = un
                        // Auto-open while work remains for the operator.
                        if (un.length > 0) zoneCard.expanded = true
                    }
                    onSnapChanged: refresh()
                    Component.onCompleted: refresh()

                    function zoneName(zoneId) {
                        if (typeof NoFlyZoneModel === "undefined") return "#" + zoneId
                        for (var i = 0; i < NoFlyZoneModel.count; ++i) {
                            var idx = NoFlyZoneModel.index(i, 0)
                            if (NoFlyZoneModel.data(idx, NoFlyZoneModelRoles.ZoneIdRole) === zoneId)
                                return NoFlyZoneModel.data(idx, NoFlyZoneModelRoles.NameRole)
                        }
                        return "#" + zoneId
                    }

                    // Overall status chip state: 0 fail, 1 pass, 2 acknowledged-pass, 3 n/a
                    readonly property int _overall: {
                        var total = _intersecting.length
                        var unacked = _unacked.length
                        if (total === 0)
                            return (_zoneCheck && _zoneCheck.status === 1) ? 1 : 3
                        return unacked > 0 ? 0 : 2
                    }

                    ColumnLayout {
                        id: zoneCol
                        width: parent.width
                        spacing: 0

                        // ── Top Half: Hardware & Connection Status ──
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: Config.spacingMedium
                            spacing: Config.spacingMedium

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                // Dot: amber=calibrating, green=connected, red=none; pulses while not ready
                                color: (typeof TelemetryProvider !== "undefined" && TelemetryProvider && TelemetryProvider.gimbalCalibrating) ? Colors.warning : ((typeof TelemetryProvider !== "undefined" && TelemetryProvider && TelemetryProvider.gimbalDetected) ? Colors.success : Colors.error)
                                border.color: Colors.textPrimary
                                border.width: 1.5
                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    running: (typeof TelemetryProvider === "undefined" || !TelemetryProvider) ? false : (!TelemetryProvider.gimbalDetected || TelemetryProvider.gimbalCalibrating)
                                    NumberAnimation { from: 1.0; to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
                                    NumberAnimation { from: 0.3; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: (typeof TelemetryProvider !== "undefined" && TelemetryProvider && TelemetryProvider.gimbalDetected) ? (TelemetryProvider.gimbalCalibrating ? "\u26A0 Calibrating Gimbal..." : "\u2713 Gimbal Connected") : "\u23F3 No Gimbal Detected"
                                    color: Colors.textPrimary
                                    font.pixelSize: Config.fontSizeBody
                                    font.bold: true
                                }
                                Text {
                                    text: (typeof TelemetryProvider !== "undefined" && TelemetryProvider && TelemetryProvider.gimbalDetected) ? "Gimbal telemetry & MAVLink control active" : "Ensure gimbal hardware is powered and connected"
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                }
                            }

                            Text {
                                id: statusToast
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                opacity: 0.0
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                        }

                        // Divider line
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Colors.border
                        }

                        // ── Bottom Half: Restricted Zones Header (click to expand/collapse) ──
                        Rectangle {
                            id: zoneHeader
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            color: zoneHeaderMa.containsMouse ? Colors.surfaceLight : "transparent"
                            radius: Config.radiusMedium

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Config.spacingMedium
                                spacing: Config.spacingMedium

                                Text {
                                    text: "\uD83D\uDEA9 Restricted Zones"
                                    font.pixelSize: Config.fontSizeBody
                                    font.bold: true
                                    color: Colors.textPrimary
                                }

                                Item { Layout.fillWidth: true }

                                Rectangle {
                                    Layout.preferredHeight: 24
                                    Layout.preferredWidth: overallTxt.implicitWidth + 16
                                    radius: Config.radiusSmall
                                    color: zoneCard._overall === 0 ? Colors.errorDim
                                         : zoneCard._overall === 1 ? Colors.successDim
                                         : zoneCard._overall === 2 ? Colors.checkWarnDim
                                                                   : Colors.surfaceLight
                                    border.color: zoneCard._overall === 0 ? Colors.error
                                                : zoneCard._overall === 1 ? Colors.success
                                                : zoneCard._overall === 2 ? Colors.checkWarn
                                                                          : Colors.border
                                    border.width: 1
                                    Text {
                                        id: overallTxt
                                        anchors.centerIn: parent
                                        text: zoneCard._overall === 0
                                              ? qsTr("FAIL \u2014 %n zone(s) to acknowledge", "", zoneCard._unacked.length)
                                              : zoneCard._overall === 1 ? qsTr("PASS \u2014 Route clear")
                                              : zoneCard._overall === 2 ? qsTr("ACKNOWLEDGED")
                                                                        : qsTr("NO MISSION LOADED")
                                        font.pixelSize: Config.fontSizeSmall
                                        font.bold: true
                                        color: zoneCard._overall === 0 ? Colors.error
                                             : zoneCard._overall === 1 ? Colors.statePass
                                             : zoneCard._overall === 2 ? Colors.checkWarn
                                                                       : Colors.textDisabled
                                    }
                                }

                                Text {
                                    text: zoneCard.expanded ? "\u25B2" : "\u25BC"
                                    font.pixelSize: 12
                                    color: Colors.textSecondary
                                }
                            }
                            MouseArea {
                                id: zoneHeaderMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: zoneCard.expanded = !zoneCard.expanded
                            }
                        }

                        // ── Expanded checklist body ──
                        ColumnLayout {
                            visible: zoneCard.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: Config.spacingMedium
                            Layout.rightMargin: Config.spacingMedium
                            Layout.bottomMargin: Config.spacingSmall
                            spacing: 6


                            Text {
                                visible: zoneCard._intersecting.length === 0
                                text: zoneCard._zoneCheck && zoneCard._zoneCheck.status !== 1
                                      ? qsTr("Load a mission in Plan View to test the route against restricted zones.")
                                      : qsTr("No restricted zones intersect the planned route.")
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textSecondary
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            // Scrollable zone list — caps at ~5 rows, scrolls beyond
                            ScrollView {
                                id: zoneScroll
                                visible: zoneCard._intersecting.length > 0
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(zoneListCol.implicitHeight, 244)
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                ColumnLayout {
                                    id: zoneListCol
                                    width: zoneScroll.availableWidth
                                    spacing: 6

                                    Repeater {
                                        model: zoneCard._intersecting

                                        delegate: Rectangle {
                                            id: zoneRow
                                            required property var modelData
                                            readonly property int zoneId: parseInt(modelData)
                                            readonly property bool acked: zoneCard._unacked.indexOf(modelData) < 0
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 40
                                            radius: Config.radiusSmall
                                            color: acked ? Colors.successDim : Colors.surfaceLight
                                            border.color: acked ? Colors.success : Colors.border
                                            border.width: 1

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: Config.spacingSmall
                                                spacing: Config.spacingSmall

                                                // Checkable tick box — ticking records the acknowledgement
                                                Rectangle {
                                                    id: zoneTick
                                                    width: 22; height: 22; radius: 4
                                                    color: zoneRow.acked ? Colors.success : "transparent"
                                                    border.color: zoneRow.acked ? Colors.success : Colors.border
                                                    border.width: 1
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: "\u2713"
                                                        font.pixelSize: 13
                                                        font.bold: true
                                                        color: Colors.background
                                                        visible: zoneRow.acked
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        enabled: !zoneRow.acked
                                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: {
                                                            if (zoneCard._zoneCheck && zoneCard._zoneCheck.acknowledgeZone(
                                                                    zoneRow.zoneId,
                                                                    qsTr("Acknowledged via payload page checklist")))
                                                                zoneCard.refresh()
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: zoneCard.zoneName(zoneRow.zoneId)
                                                    font.pixelSize: Config.fontSizeBody
                                                    font.bold: true
                                                    color: Colors.textPrimary
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    text: zoneRow.acked
                                                          ? qsTr("\u2713 Recorded to compliance log")
                                                          : qsTr("Tick to acknowledge crossing")
                                                    font.pixelSize: Config.fontSizeSmall
                                                    color: zoneRow.acked ? Colors.statePass : Colors.textSecondary
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Acknowledgements are stored in the compliance audit log (operator · zone · time · flight). Organizational record only — no geofence is uploaded to the vehicle.")
                                font.pixelSize: Config.fontSizeSmall - 1
                                color: Colors.textDisabled
                                wrapMode: Text.WordWrap
                            }
                    }
                }
            }

                // ── Battery Time Estimator ──
                BatteryTimeEstimatorPanel {
                    Layout.fillWidth: true
                    visible: false
                }

                // ── Three Numbers: the UAV's live Pitch / Roll / Yaw (degrees) ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingMedium
                        spacing: Config.spacingMedium

                        // Pitch
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: "PITCH"
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: (root._uavPitchDeg).toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    // Which attitude this is.
                                    text: root._attFromGimbal ? qsTr("gimbal") : qsTr("UAV")
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // Roll
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: "ROLL"
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: (root._uavRollDeg).toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    // Which attitude this is.
                                    text: root._attFromGimbal ? qsTr("gimbal") : qsTr("UAV")
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // Yaw
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: "YAW"
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: (root._uavYawDeg).toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    // Which attitude this is.
                                    text: root._attFromGimbal ? qsTr("gimbal") : qsTr("UAV")
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }
                }

                // ── Gimbal Test Buttons (no Recalibrate) ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: gimbalTestsCol.implicitHeight + Config.spacingMedium * 2
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

                    ColumnLayout {
                        id: gimbalTestsCol
                        anchors { top: parent.top; left: parent.left; right: parent.right; margins: Config.spacingMedium }
                        spacing: Config.spacingSmall

                        Text {
                            text: "\uD83D\uDEE0 Gimbal Tests"
                            font.pixelSize: Config.fontSizeBody
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Config.spacingSmall

                            Button {
                                width: (parent.width - 4 * parent.spacing) / 5
                                height: 42
                                text: "\uD83C\uDFAF " + qsTr("Center")
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                enabled: root._gimbalCmdSupported && !root.axisTestRunning
                                onClicked: {
                                    // mode 2 = MAVLINK_TARGETING; zero angles center the gimbal
                                    TelemetryProvider.sendMountControl(0, 0, 0, 2);
                                    root._send("Center Gimbal (p=0, r=0, y=0, mode=2)");
                                }
                            }
                            Repeater {
                                model: [ { axis: "pitch", label: qsTr("Pitch test") },
                                         { axis: "roll",  label: qsTr("Roll test") },
                                         { axis: "yaw",   label: qsTr("Yaw test") } ]
                                delegate: Button {
                                    required property var modelData
                                    width: (parent.width - 4 * parent.spacing) / 5
                                    height: 42
                                    text: modelData.label
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    enabled: root._gimbalCmdSupported && !root.axisTestRunning
                                    onClicked: root.runAxisTests([modelData.axis])
                                }
                            }
                            Button {
                                width: (parent.width - 4 * parent.spacing) / 5
                                height: 42
                                text: root.axisTestRunning ? qsTr("Testing…") : qsTr("Test all axes")
                                highlighted: true
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                enabled: root._gimbalCmdSupported && !root.axisTestRunning
                                onClicked: root.runAxisTests(["pitch", "roll", "yaw"])
                            }
                        }

                        // Per-axis results
                        Repeater {
                            model: ["pitch", "roll", "yaw"]
                            delegate: RowLayout {
                                required property string modelData
                                readonly property var res: root._axisResults[modelData]
                                Layout.fillWidth: true
                                visible: res !== undefined
                                spacing: Config.spacingSmall
                                Text {
                                    text: modelData.toUpperCase()
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 50
                                }
                                Text {
                                    text: !res ? "" : res.state === "pass" ? "PASS" : res.state === "fail" ? "FAIL"
                                                    : res.state === "skip" ? "SKIP" : "\u23F3"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: !res ? Colors.textSecondary : res.state === "pass" ? Colors.success
                                         : res.state === "fail" ? Colors.error : Colors.textSecondary
                                    Layout.preferredWidth: 44
                                }
                                Text {
                                    text: res ? res.text : ""
                                    font.pixelSize: Config.fontSizeSmall
                                    color: Colors.textPrimary
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !root._gimbalCmdSupported
                            text: qsTr("Gimbal test not available yet — needs gimbal command support in the GCS.")
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.statusWarn
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // ── Control Surface Sweep ──
                Rectangle {
                    id: surfaceCard
                    Layout.fillWidth: true
                    visible: !root._isMultirotor
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1
                    // implicitHeight, not height: the page's ColumnLayout ignores
                    // `height`, so the card was laid out ~0 tall, its tests spilled
                    // past the ScrollView content and the last ones were unreachable.
                    implicitHeight: surfaceCol.implicitHeight + Config.spacingMedium * 2

                    ColumnLayout {
                        id: surfaceCol
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: Config.spacingMedium
                        }
                        spacing: Config.spacingSmall

                        // Header row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Config.spacingMedium

                            Text {
                                text: "\uD83D\uDC49 Control Surface Test"
                                color: Colors.textPrimary
                                font.pixelSize: Config.fontSizeBody
                                font.bold: true
                                Layout.fillWidth: true
                            }

                            Text {
                                text: root._surfaces.length + (root._surfaces.length === 1 ? " surface" : " surfaces")
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                color: root._surfaces.length > 0 ? Colors.textSecondary : Colors.textDisabled
                            }

                            Button {
                                text: "Center All"
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                Layout.preferredHeight: 34
                                enabled: !root._flying
                                onClicked: root._safeStopAllSurfaces()
                            }
                        }

                        // Why the Sweep buttons are disabled, in plain sight (the
                        // tooltip is invisible on a touchscreen).
                        Text {
                            Layout.fillWidth: true
                            visible: typeof ControlSurfaceTestController !== "undefined"
                                     && ControlSurfaceTestController.testBlockedReason.length > 0
                            text: "\u26A0 " + ControlSurfaceTestController.testBlockedReason
                            wrapMode: Text.WordWrap
                            font.pixelSize: Config.fontSizeSmall
                            font.bold: true
                            color: Colors.warning
                        }

                        // Flight-mode + live feedback status strip
                        Rectangle {
                            Layout.fillWidth: true
                            visible: root._surfaces.length > 0
                            implicitHeight: 28
                            radius: Config.radiusSmall
                            color: Colors.background
                            border.width: 1
                            border.color: Colors.borderLight

                            property string _mode: (typeof ControlSurfaceTestController !== "undefined") ? ControlSurfaceTestController.flightMode : ""
                            // Manual-class modes = no autopilot authority over the swept outputs.
                            property bool _manual: _mode.length === 0
                                    || _mode.toLowerCase().indexOf("manual") >= 0
                                    || _mode.toLowerCase().indexOf("stabilize") >= 0
                                    || _mode.toLowerCase().indexOf("fbwa") >= 0
                                    || _mode.toLowerCase().indexOf("training") >= 0
                                    || _mode.toLowerCase().indexOf("acro") >= 0
                                    || _mode.toLowerCase().indexOf("cruise") >= 0
                                    || _mode.toLowerCase().indexOf("autotune") >= 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 8

                                Text {
                                    text: "\u2708 " + (parent.parent._mode.length > 0 ? parent.parent._mode : "mode \u2026")
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: parent.parent._manual ? Colors.textPrimary : Colors.error
                                }

                                Rectangle {
                                    visible: !parent.parent._manual
                                    implicitWidth: manualBtnText.implicitWidth + 16
                                    implicitHeight: 22
                                    radius: 4
                                    color: manualMa.containsMouse ? Colors.danger : Colors.error
                                    border.color: Colors.statusFail
                                    border.width: 1

                                    Text {
                                        id: manualBtnText
                                        anchors.centerIn: parent
                                        text: "⚡ Switch to MANUAL"
                                        font.pixelSize: Config.fontSizeSmall
                                        font.bold: true
                                        color: Colors.textPrimary
                                    }

                                    MouseArea {
                                        id: manualMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof ControlSurfaceTestController !== "undefined") {
                                                ControlSurfaceTestController.switchToManual()
                                            }
                                            var v = QGroundControl.multiVehicleManager.activeVehicle
                                            if (v && typeof v.setFlightMode === "function") {
                                                v.setFlightMode("MANUAL")
                                            }
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: {
                                        var pwm = (typeof ControlSurfaceTestController !== "undefined")
                                                ? ControlSurfaceTestController.sweptPwm : 0
                                        if (pwm === 0) return ""
                                        return "CH" + (ControlSurfaceTestController.activeSurface >= 0
                                                        ? ControlSurfaceTestController.surfaces[ControlSurfaceTestController.activeSurface].channel
                                                        : "?") + " = " + pwm + " \u00b5s"
                                    }
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                }
                            }
                        }

                        // Armed warning
                        Rectangle {
                            Layout.fillWidth: true
                            visible: root._armed
                            height: 30
                            radius: Config.radiusSmall
                            color: Colors.warning
                            Text {
                                anchors.centerIn: parent
                                text: "Vehicle is ARMED \u2014 disarm before surface sweep"
                                color: Colors.background
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                            }
                        }

                        // Empty / warning state
                        Text {
                            Layout.fillWidth: true
                            visible: root._surfaces.length === 0
                            text: "No control surfaces mapped for this airframe."
                            font.pixelSize: Config.fontSizeBody
                            color: Colors.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // Per-surface test rows
                        Repeater {
                            model: root._surfaces

                            Rectangle {
                                id: scard
                                required property var modelData
                                required property int index
                                readonly property int st: (root._states.length > index) ? root._states[index] : 0

                                Layout.fillWidth: true
                                implicitHeight: scol.implicitHeight + 12
                                radius: Config.radiusSmall
                                border.width: 1
                                border.color: st === 1 ? Colors.testing
                                            : st === 2 ? Colors.success
                                            : st === 3 ? Colors.error
                                            : Colors.border
                                color: st === 2 ? Colors.successDim
                                     : st === 3 ? Colors.errorDim
                                     : Colors.surfaceLight

                                ColumnLayout {
                                    id: scol
                                    anchors { fill: parent; margins: 8 }
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Config.spacingSmall

                                        Text {
                                            text: scard.modelData.label
                                            font.pixelSize: Config.fontSizeBody
                                            font.bold: true
                                            color: Colors.textPrimary
                                            Layout.fillWidth: true
                                        }

                                        Rectangle {
                                            Layout.preferredHeight: 20
                                            Layout.preferredWidth: chTxt.implicitWidth + 10
                                            radius: 10
                                            color: Colors.surface
                                            border.color: Colors.border
                                            border.width: 1
                                            Text {
                                                id: chTxt
                                                anchors.centerIn: parent
                                                text: "CH" + scard.modelData.channel
                                                font.pixelSize: Config.fontSizeSmall
                                                color: Colors.textSecondary
                                            }
                                        }

                                        Rectangle {
                                            visible: scard.st !== 0
                                            Layout.preferredHeight: 20
                                            Layout.preferredWidth: stTxt.implicitWidth + 10
                                            radius: 10
                                            color: scard.st === 1 ? Colors.testing
                                                 : scard.st === 2 ? Colors.success
                                                 : scard.st === 3 ? Colors.error
                                                 : "transparent"
                                            Text {
                                                id: stTxt
                                                anchors.centerIn: parent
                                                font.pixelSize: 10; font.bold: true; color: Colors.background
                                                text: scard.st === 1 ? qsTr("Sweeping\u2026")
                                                    : scard.st === 2 ? qsTr("\u2713 Pass")
                                                    : scard.st === 3 ? qsTr("\u2717 Fail / Reversed")
                                                    : ""
                                            }
                                        }

                                        Button {
                                            text: scard.st === 1 ? "SWEEPING\u2026" : "Sweep"
                                            highlighted: scard.st === 0
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                            Layout.preferredHeight: 30
                                            Layout.preferredWidth: 96
                                            // B4/B5: the controller says why a sweep cannot start (no
                                            // vehicle, armed by another source, no Testing session);
                                            // only one surface sweeps at a time.
                                            enabled: ControlSurfaceTestController.testBlockedReason.length === 0
                                                     && ControlSurfaceTestController.activeSurface === -1
                                            ToolTip.visible: hovered && ControlSurfaceTestController.testBlockedReason.length > 0
                                            ToolTip.text: ControlSurfaceTestController.testBlockedReason
                                            onClicked: {
                                                if (typeof ControlSurfaceTestController === "undefined") return
                                                // Dispatch to the per-surface test method (C++ controller)
                                                switch (scard.modelData.id) {
                                                case "aileron":    ControlSurfaceTestController.testAileron();    break
                                                case "elevator":   ControlSurfaceTestController.testElevator();   break
                                                case "rudder":     ControlSurfaceTestController.testRudder();     break
                                                case "nose_wheel": ControlSurfaceTestController.testNoseWheel(); break
                                                case "elevon_l":   ControlSurfaceTestController.testElevonLeft(); break
                                                case "elevon_r":   ControlSurfaceTestController.testElevonRight(); break
                                                case "flap":       ControlSurfaceTestController.testFlap();       break
                                                default:           ControlSurfaceTestController.testSurface(scard.modelData.id)
                                                }
                                                root._send("Sweeping " + scard.modelData.label + " (CH" + scard.modelData.channel + ")")
                                            }
                                        }
                                    }

                                    // Direction verification panel (shown after the sweep cycle finishes)
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: scard.st === 1
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: scard.modelData.question && scard.modelData.question.length > 0
                                                ? scard.modelData.question
                                                : qsTr("Did the surface move in the correct direction?")
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                            color: Colors.textPrimary
                                            wrapMode: Text.WordWrap
                                        }

                                        Button {
                                            text: qsTr("\u2713 Yes")
                                            highlighted: true
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                            Layout.preferredHeight: 30
                                            onClicked: ControlSurfaceTestController.confirmSurfaceDirection(scard.modelData.id, true)
                                        }

                                        Button {
                                            text: qsTr("\u2717 No")
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                            Layout.preferredHeight: 30
                                            onClicked: ControlSurfaceTestController.confirmSurfaceDirection(scard.modelData.id, false)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            }
        }
    }
}

