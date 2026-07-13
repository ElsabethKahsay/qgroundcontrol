import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    anchors { fill: parent }

    // ── Pinkish-purple gradient background ──
    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#e879f9" }
            GradientStop { position: 1.0; color: "#d946ef" }
        }
    }

    readonly property string _modeLabel: {
        switch (TelemetryProvider.gimbalMode) {
        case 0: return "Retract"
        case 1: return "Neutral"
        case 2: return "RC Targeting"
        case 3: return "GPS Point"
        default: return "Unknown (" + TelemetryProvider.gimbalMode + ")"
        }
    }

    readonly property var _ackLabels: ({
        0: "Accepted", 1: "Temp Rejected", 2: "Denied",
        3: "Unsupported", 4: "Failed", 5: "In Progress",
        6: "Cancelled", 7: "Only"
    })

    property string _lastCmd: ""
    property string _lastAck: ""
    property string joystickStatusText: "Drag to steer payload in real-time"

    function _send(cmd) {
        _lastCmd = cmd
        _lastAck = "sent..."
        statusToast.text = cmd + " \u2192 sent"
        statusToast.color = Colors.info
        statusToast.opacity = 1.0
        ackTimer.restart()
    }

    function updateJoystickStatus(pitch, yaw) {
        joystickStatusText = "Manual: Pitch " + pitch.toFixed(0) + "\u00B0 | Yaw " + yaw.toFixed(0) + "\u00B0"
    }

    Timer {
        id: ackTimer
        interval: 4000
        onTriggered: { statusToast.opacity = 0.0 }
    }

    Connections {
        target: TelemetryProvider
        function onGimbalCommandResult(command, result) {
            if (command !== 205) return
            var label = root._ackLabels[result] || ("Unknown(" + result + ")")
            root._lastAck = label
            statusToast.text = root._lastCmd + " \u2192 " + label
            statusToast.color = result === 0 ? Colors.success : Colors.warning
            statusToast.opacity = 1.0
            ackTimer.restart()
        }
    }

    // ── Full-height scrollable layout ──
    ScrollView {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        clip: true
        contentWidth: parent.width - Config.spacingLarge * 2

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingLarge

            // ── HEADER CARD ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                color: Colors.surface
                radius: Config.radiusMedium
                border.color: Colors.border; border.width: 1

                RowLayout {
                    anchors.fill: parent; anchors.margins: Config.spacingMedium
                    spacing: Config.spacingLarge

                    // Pulsing LED
                    Rectangle {
                        width: 16; height: 16; radius: 8
                        color: TelemetryProvider.gimbalCalibrating ? Colors.warning : TelemetryProvider.gimbalDetected ? Colors.success : Colors.error
                        border.color: Colors.textPrimary; border.width: 1.5
                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            running: !TelemetryProvider.gimbalDetected || TelemetryProvider.gimbalCalibrating
                            NumberAnimation { from: 1.0; to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Text {
                            text: TelemetryProvider.gimbalDetected
                                ? (TelemetryProvider.gimbalCalibrating ? "\u26A0 Calibrating..." : "\u2713 Camera Gimbal Online")
                                : "\u23F3 No Gimbal Detected"
                            color: Colors.textPrimary; font.pixelSize: Config.fontSizeBody; font.bold: true
                        }
                        Text {
                            text: TelemetryProvider.gimbalDetected
                                ? "MAVLink mount control interface active"
                                : "Set MNT_TYPE=3 or use Auto-Setup below"
                            color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall
                        }
                    }

                    Text {
                        id: statusToast
                        font.pixelSize: Config.fontSizeSmall; font.bold: true
                        opacity: 0.0; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        NumberAnimation on opacity { from: 1.0; to: 0.0; duration: 3000; running: false }
                    }
                }
            }

            // ── MAIN CONTENT GRID ──
            GridLayout {
                Layout.fillWidth: true
                columns: parent.width >= 720 ? 2 : 1
                rowSpacing: Config.spacingLarge
                columnSpacing: Config.spacingLarge

                // ── LEFT: INSTRUMENTS ──
                Rectangle {
                    Layout.fillWidth: true; Layout.minimumWidth: 280
                    Layout.preferredHeight: 380
                    color: Colors.surface; radius: Config.radiusMedium
                    border.color: Colors.border; border.width: 1

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingLarge
                        spacing: Config.spacingMedium

                        Text { text: "\uD83D\uDCCA Avionics HUD"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary }

                        RowLayout {
                            Layout.fillWidth: true; spacing: Config.spacingLarge
                            Layout.alignment: Qt.AlignHCenter

                            // Horizon sphere
                            Rectangle {
                                width: 130; height: 130; radius: 65
                                color: "#1e293b"; border.color: Colors.border; border.width: 2; clip: true
                                Item {
                                    id: horizonBall
                                    width: 240; height: 240; anchors.centerIn: parent
                                    transform: [
                                        Translate { y: TelemetryProvider.gimbalPitch * 1.5 },
                                        Rotation { angle: -TelemetryProvider.gimbalRoll; origin.x: 120; origin.y: 120 }
                                    ]
                                    Rectangle { width: 240; height: 120; y: 0; color: "#0D47A1" }
                                    Rectangle { width: 240; height: 120; y: 120; color: "#3E2723" }
                                    Rectangle { width: 240; height: 2; y: 119; color: Colors.textPrimary }
                                }
                                Rectangle { anchors.centerIn: parent; width: 10; height: 2; color: Colors.accent }
                                Rectangle { anchors.centerIn: parent; width: 2; height: 10; color: Colors.accent }
                                Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: Colors.textPrimary }
                            }

                            // Compass rose
                            Rectangle {
                                width: 130; height: 130; radius: 65
                                color: "#1e293b"; border.color: Colors.border; border.width: 2; clip: true
                                Item {
                                    id: compassCard
                                    width: 114; height: 114; anchors.centerIn: parent
                                    rotation: -TelemetryProvider.gimbalYaw
                                    Behavior on rotation { RotationAnimation { duration: 150; direction: RotationAnimation.Shortest } }
                                    Rectangle { anchors.fill: parent; radius: 57; color: "transparent"; border.color: "#22334155"; border.width: 1 }
                                    Repeater {
                                        model: [{a:0,l:"N"},{a:90,l:"E"},{a:180,l:"S"},{a:270,l:"W"}]
                                        delegate: Item {
                                            width: 114; height: 114; anchors.centerIn: parent; rotation: modelData.a
                                            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 2; width: 2; height: 5; color: modelData.l === "N" ? Colors.accent : Colors.textSecondary }
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 9; text: modelData.l; color: Colors.textPrimary; font.bold: true; font.pixelSize: 10; transform: Rotation { angle: -modelData.a; origin.x: width/2; origin.y: height/2 } }
                                        }
                                    }
                                }
                                Rectangle { anchors.centerIn: parent; width: 36; height: 18; radius: 3; color: "#B01e293b"; border.color: Colors.border; border.width: 1
                                    Text { anchors.centerIn: parent; text: Math.round(TelemetryProvider.gimbalYaw).toString().padStart(3,'0') + "\u00B0"; color: Colors.textPrimary; font.pixelSize: 10; font.bold: true; font.family: "monospace" }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true; columns: 2; rowSpacing: Config.spacingSmall; columnSpacing: Config.spacingLarge
                            Text { text: "Pitch:"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                            Text { text: TelemetryProvider.gimbalPitch.toFixed(1) + "\u00B0"; color: Colors.textPrimary; font.bold: true; font.family: "monospace"; font.pixelSize: Config.fontSizeSmall }
                            Text { text: "Roll:"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                            Text { text: TelemetryProvider.gimbalRoll.toFixed(1) + "\u00B0"; color: Colors.textPrimary; font.bold: true; font.family: "monospace"; font.pixelSize: Config.fontSizeSmall }
                            Text { text: "Yaw:"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                            Text { text: TelemetryProvider.gimbalYaw.toFixed(1) + "\u00B0"; color: Colors.textPrimary; font.bold: true; font.family: "monospace"; font.pixelSize: Config.fontSizeSmall }
                            Text { text: "Mode:"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                            Text { text: root._modeLabel; color: Colors.accent; font.bold: true; font.pixelSize: Config.fontSizeSmall }
                            Text { text: "Cal:"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                            Text { text: TelemetryProvider.gimbalCalibrating ? "Calibrating..." : "OK"; color: TelemetryProvider.gimbalCalibrating ? Colors.warning : Colors.success; font.bold: true; font.pixelSize: Config.fontSizeSmall }
                        }
                    }
                }

                // ── RIGHT: CONTROLS ──
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 240
                    spacing: Config.spacingLarge

                    // Joystick
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 200
                        color: Colors.surface; radius: Config.radiusMedium
                        border.color: Colors.border; border.width: 1

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingMedium; spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "\uD83D\uDD79 Joystick Control"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary; Layout.fillWidth: true }
                                Text { text: "Auto-Center"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall }
                                Switch { id: springToggle; checked: true; palette.active.highlight: Colors.accent }
                            }

                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                color: Colors.surface; radius: Config.radiusSmall; border.color: Colors.border; clip: true
                                Rectangle { anchors.centerIn: parent; width: parent.width - 24; height: 1; color: "#22334155" }
                                Rectangle { anchors.centerIn: parent; width: 1; height: parent.height - 24; color: "#22334155" }

                                Item {
                                    id: joystickPad
                                    anchors.fill: parent; anchors.margins: 12
                                    readonly property real centerX: width / 2
                                    readonly property real centerY: height / 2
                                    readonly property real maxRadius: Math.min(width, height) / 2 - 10

                                    Rectangle {
                                        id: joystickKnob
                                        x: joystickPad.centerX - width/2 + offset.x
                                        y: joystickPad.centerY - height/2 + offset.y
                                        width: 36; height: 36; radius: 18
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: Colors.accent }
                                            GradientStop { position: 1.0; color: Colors.pastelPurple }
                                        }
                                        border.color: Colors.textPrimary; border.width: 1.5
                                        property point offset: Qt.point(0,0)
                                        Behavior on offset { enabled: !dragArea.pressed; PropertyAnimation { duration: 150; easing.type: Easing.OutQuad } }
                                        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Colors.textPrimary; opacity: 0.7 }
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent; preventStealing: true
                                        onPositionChanged: (mouse) => {
                                            var dx = mouse.x - joystickPad.centerX
                                            var dy = mouse.y - joystickPad.centerY
                                            var dist = Math.sqrt(dx*dx + dy*dy)
                                            if (dist > joystickPad.maxRadius) { dx = dx/dist * joystickPad.maxRadius; dy = dy/dist * joystickPad.maxRadius }
                                            joystickKnob.offset = Qt.point(dx, dy)
                                            var normX = dx / joystickPad.maxRadius
                                            var normY = dy / joystickPad.maxRadius
                                            var targetPitch = normY < 0 ? normY * 90.0 : normY * 45.0
                                            var targetYaw = normX * 180.0
                                            TelemetryProvider.sendMountControl(targetPitch, 0, targetYaw, TelemetryProvider.gimbalMode)
                                            root.updateJoystickStatus(targetPitch, targetYaw)
                                        }
                                        onReleased: {
                                            if (springToggle.checked) {
                                                joystickKnob.offset = Qt.point(0,0)
                                                TelemetryProvider.sendMountControl(0,0,0,1)
                                                root._send("Recenter")
                                                joystickStatusText = "Mount returned to neutral"
                                            }
                                        }
                                    }
                                }
                                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 6; text: "\u25C0 Yaw"; color: Colors.textDisabled; font.pixelSize: 9; font.bold: true }
                                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 6; text: "Yaw \u25B6"; color: Colors.textDisabled; font.pixelSize: 9; font.bold: true }
                                Text { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 4; text: "\u25B2 Pitch"; color: Colors.textDisabled; font.pixelSize: 9; font.bold: true }
                                Text { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 4; text: "\u25BC Pitch"; color: Colors.textDisabled; font.pixelSize: 9; font.bold: true }
                            }

                            Text { text: root.joystickStatusText; color: dragArea.pressed ? Colors.accent : Colors.textSecondary; font.pixelSize: Config.fontSizeSmall; Layout.alignment: Qt.AlignHCenter; font.bold: dragArea.pressed }
                        }
                    }

                    // Mode selector
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 100
                        color: Colors.surface; radius: Config.radiusMedium
                        border.color: Colors.border; border.width: 1

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingMedium; spacing: Config.spacingMedium
                            Text { text: "\uD83D\uDEE0 Mount Modes"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary }
                            RowLayout {
                                Layout.fillWidth: true; spacing: Config.spacingSmall
                                Repeater {
                                    model: [
                                        { idx: 0, lbl: "Retract" }, { idx: 1, lbl: "Neutral" },
                                        { idx: 2, lbl: "RC Tgt" }, { idx: 3, lbl: "GPS Pt" }
                                    ]
                                    delegate: Button {
                                        Layout.fillWidth: true
                                        text: modelData.lbl
                                        highlighted: TelemetryProvider.gimbalMode === modelData.idx
                                        font.pixelSize: Config.fontSizeSmall
                                        onClicked: {
                                            TelemetryProvider.setGimbalMode(modelData.idx)
                                            root._send("Mode: " + modelData.lbl)
                                            if (modelData.idx === 1) TelemetryProvider.sendMountControl(0,0,0,1)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── SITL Config ──
            Rectangle {
                visible: !TelemetryProvider.gimbalDetected
                Layout.fillWidth: true; Layout.maximumHeight: 160
                color: Colors.surface; radius: Config.radiusMedium
                border.color: Colors.border; border.width: 1

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Config.spacingLarge; spacing: Config.spacingSmall
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "\uD83D\uDCBB SITL Configuration"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.warning; Layout.fillWidth: true }
                        Rectangle { width: 50; height: 18; radius: 3; color: Colors.warningDim
                            Text { anchors.centerIn: parent; text: "SITL"; color: Colors.warning; font.pixelSize: 10; font.bold: true }
                        }
                    }
                    Text { text: "Enable simulated gimbal via MNT_TYPE=3, MNT_DEFAULTS=1, BRD_SAFETYENABLE=0"; color: Colors.textSecondary; font.pixelSize: Config.fontSizeSmall; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true; spacing: Config.spacingLarge
                        Button {
                            text: "\u26A1 Auto-Configure"
                            highlighted: true; font.pixelSize: Config.fontSizeSmall
                            Layout.preferredHeight: 40
                            onClicked: {
                                TelemetryProvider.setParameter("MNT_TYPE", 3)
                                TelemetryProvider.setParameter("MNT_DEFAULTS", 1)
                                TelemetryProvider.setParameter("BRD_SAFETYENABLE", 0)
                                root._send("SITL config sent")
                            }
                        }

                        // ── Gimbal axis test buttons ──
                        Button {
                            text: "Pitch Test"
                            font.pixelSize: Config.fontSizeSmall
                            Layout.preferredHeight: 40
                            onClicked: {
                                // MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW = 1000
                                TelemetryProvider.sendGimbalManagerCommand(45.0, 0.0, 0)
                                root._send("Gimbal Pitch +45\u00B0")
                            }
                        }
                        Button {
                            text: "Yaw Test"
                            font.pixelSize: Config.fontSizeSmall
                            Layout.preferredHeight: 40
                            onClicked: {
                                TelemetryProvider.sendGimbalManagerCommand(0.0, 90.0, 0)
                                root._send("Gimbal Yaw +90\u00B0")
                            }
                        }
                    }
                }
            }

            // ── Gimbal dedicated action buttons ──
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 100
                color: Colors.surface; radius: Config.radiusMedium
                border.color: Colors.border; border.width: 1

                RowLayout {
                    anchors.fill: parent; anchors.margins: Config.spacingLarge
                    spacing: Config.spacingLarge

                    ColumnLayout {
                        Layout.fillWidth: true; spacing: Config.spacingSmall
                        Text { text: "\u2699 Gimbal Actions"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary }
                        Text { text: "Dedicated MAVLink commands for gimbal control"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                    }

                    Button {
                        text: "\uD83D\uDCF7 Capture"
                        highlighted: true; font.pixelSize: Config.fontSizeSmall
                        implicitHeight: 40
                        onClicked: {
                            // MAV_CMD_IMAGE_START_CAPTURE = 2000
                            TelemetryProvider.sendImageCaptureCommand(1, 0)
                            root._send("Image capture triggered")
                        }
                    }

                    Button {
                        text: "\u2699\uFE0F Recalibrate"
                        highlighted: true; font.pixelSize: Config.fontSizeSmall
                        implicitHeight: 40
                        onClicked: {
                            // MAV_CMD_DO_GIMBAL_CALIBRATION = 205
                            TelemetryProvider.sendGimbalCalibrationCommand()
                            root._send("Gimbal calibration started")
                        }
                    }

                    Button {
                        text: "\uD83D\uDEE1 Unlock"
                        font.pixelSize: Config.fontSizeSmall
                        implicitHeight: 40
                        onClicked: {
                            TelemetryProvider.sendMountControl(0, 0, 0, 1)
                            root._send("Gimbal unlock / neutral")
                        }
                    }
                }
            }

            // ── Divider ──
            Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }

            // ── Bottom nav ──
            RowLayout {
                Layout.fillWidth: true; spacing: Config.spacingLarge
                Button {
                    text: "\u2190 Back to Checklist"
                    Layout.fillWidth: true; Layout.preferredHeight: 44
                    font.pixelSize: Config.fontSizeBody
                    onClicked: Window.window.mainStackView.pop()
                }
                Button {
                    text: "Launch Ready \u2192"
                    Layout.fillWidth: true; Layout.preferredHeight: 44
                    font.pixelSize: Config.fontSizeBody; highlighted: true
                    onClicked: Window.window.mainStackView.push("LaunchReady.qml")
                }
            }
        }
    }
}
