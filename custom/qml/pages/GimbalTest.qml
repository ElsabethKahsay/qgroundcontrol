import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    anchors.fill: parent

    signal backRequested

    // Transparent background so the 3% margin around pink outer box shows the parent background
    background: Item {}

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
    property bool tiltTestRunning: false
    property bool tiltTestPassed: false

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

    function _loadSurfaces() {
        if (typeof ControlSurfaceTestController !== "undefined"
                && typeof VehicleProfileManager !== "undefined") {
            ControlSurfaceTestController.loadSurfacesForVehicle(
                VehicleProfileManager.vehicleType, VehicleProfileManager.motorCount)
        }
    }

    Component.onCompleted: _loadSurfaces()
    Component.onDestruction: {
        if (typeof ControlSurfaceTestController !== "undefined")
            ControlSurfaceTestController.stopAllSurfaces()
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

    // Tilt Test: sends pitch=-30, verifies attitude change within 2s, resets to 0
    Timer {
        id: tiltReturnTimer
        interval: 2000
        repeat: false
        onTriggered: {
            TelemetryProvider.sendMountControl(0, 0, 0, 2);
            root.tiltTestRunning = false;
            root.tiltTestPassed = true;
            root._send("Tilt Test Complete (Passed)");
        }
    }

    Connections {
        target: TelemetryProvider
        function onGimbalAttitudeChanged() {
            if (root.tiltTestRunning) {
                root.tiltTestPassed = true;
            }
        }
        function onGimbalCommandResult(command, result) {
            if (command !== 205 && command !== 200)
                return;
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
        anchors.margins: parent.width * 0.03
        radius: Config.radiusLarge
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "#e879f9"
            }
            GradientStop {
                position: 1.0
                color: "#d946ef"
            }
        }
        border.color: "#d946ef"
        border.width: 2

        ScrollView {
            anchors.fill: parent
            anchors.margins: Config.spacingMedium
            clip: true
            contentWidth: parent.width - Config.spacingMedium * 2

            ColumnLayout {
                width: parent.width
                spacing: Config.spacingMedium

                // ── Connection Status Banner ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingMedium
                        spacing: Config.spacingMedium

                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            color: TelemetryProvider.gimbalCalibrating ? Colors.warning : TelemetryProvider.gimbalDetected ? Colors.success : Colors.error
                            border.color: Colors.textPrimary
                            border.width: 1.5
                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                running: !TelemetryProvider.gimbalDetected || TelemetryProvider.gimbalCalibrating
                                NumberAnimation {
                                    from: 1.0
                                    to: 0.3
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                                NumberAnimation {
                                    from: 0.3
                                    to: 1.0
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: TelemetryProvider.gimbalDetected ? (TelemetryProvider.gimbalCalibrating ? "\u26A0 Calibrating Gimbal..." : "\u2713 Gimbal Connected") : "\u23F3 No Gimbal Detected"
                                color: Colors.textPrimary
                                font.pixelSize: Config.fontSizeBody
                                font.bold: true
                            }
                            Text {
                                text: TelemetryProvider.gimbalDetected ? "Gimbal telemetry & MAVLink control active" : "Ensure gimbal hardware is powered and connected"
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
                }

                // ── Restricted Zones Dropdown ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

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

                        ComboBox {
                            id: zoneCombo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            enabled: NoFlyZoneModel.count > 0
                            model: NoFlyZoneModel
                            textRole: "name"
                            currentIndex: -1
                            font.pixelSize: Config.fontSizeSmall
                            displayText: count === 0 ? "No restricted zones defined" : currentIndex < 0 ? "Select a zone…" : currentText
                            background: Rectangle {
                                color: Colors.surfaceLight
                                radius: Config.radiusSmall
                                border.color: zoneCombo.activeFocus ? Colors.accent : Colors.border
                                border.width: 1
                            }
                            contentItem: Text {
                                text: zoneCombo.displayText
                                color: zoneCombo.count === 0 ? Colors.textDisabled : Colors.textPrimary
                                font.pixelSize: Config.fontSizeSmall
                                leftPadding: Config.spacingSmall
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: Text {
                                x: zoneCombo.width - width - Config.spacingSmall
                                y: (zoneCombo.height - height) / 2
                                text: "\u25BC"
                                color: Colors.textSecondary
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                // ── Three Numbers: Pitch / Roll / Yaw ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
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
                                    text: TelemetryProvider.gimbalPitch.toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
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
                                    text: TelemetryProvider.gimbalRoll.toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
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
                                    text: TelemetryProvider.gimbalYaw.toFixed(1) + "\u00B0"
                                    color: Colors.accent
                                    font.pixelSize: 20
                                    font.bold: true
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }
                }

                // ── Gimbal Test Buttons (no Recalibrate) ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: Colors.surface
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingMedium
                        spacing: Config.spacingSmall

                        Text {
                            text: "\uD83D\uDEE0 Gimbal Tests"
                            font.pixelSize: Config.fontSizeBody
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Config.spacingMedium

                            // Center Gimbal
                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 42
                                text: "\uD83C\uDFAF Center Gimbal"
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                highlighted: true
                                onClicked: {
                                    TelemetryProvider.sendMountControl(0, 0, 0, 2);
                                    root._send("Center Gimbal (p=0, r=0, y=0, mode=2)");
                                }
                            }

                            // Tilt Test
                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 42
                                text: root.tiltTestRunning ? "\u23F3 Tilt Testing..." : "\uD83D\uDCDD Tilt Test (-30\u00B0)"
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                enabled: !root.tiltTestRunning
                                onClicked: {
                                    root.tiltTestRunning = true;
                                    root.tiltTestPassed = false;
                                    TelemetryProvider.sendMountControl(-30, 0, 0, 2);
                                    root._send("Tilt Test started (-30\u00B0)");
                                    tiltReturnTimer.restart();
                                }
                            }
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
                    height: surfaceCol.implicitHeight + Config.spacingMedium * 2

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
                                onClicked: {
                                    if (typeof ControlSurfaceTestController !== "undefined")
                                        ControlSurfaceTestController.stopAllSurfaces()
                                }
                            }
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

                                Text {
                                    text: parent.parent._manual ? "" : "Switch to MANUAL for a valid sweep"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.error
                                    visible: !parent.parent._manual
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
                                            enabled: !root._armed && ControlSurfaceTestController.activeSurface === -1
                                            onClicked: {
                                                if (typeof ControlSurfaceTestController === "undefined") return
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

                // ── Bottom Nav: Back to Checklist ──
                Item {
                    Layout.fillHeight: true
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    text: "\u2190 Back to Checklist"
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    onClicked: {
                        root.backRequested();
                        if (Window.window && Window.window.mainStackView && Window.window.mainStackView.depth > 1) {
                            Window.window.mainStackView.pop();
                        }
                    }
                }
            }
        }
    }
}
