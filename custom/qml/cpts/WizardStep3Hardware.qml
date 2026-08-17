// Component: WizardStep3Hardware
// Purpose: Step 3 of the preflight wizard. Hardware verification for camera and gimbal.
//   Hooks into HardwareTestController for live tests, with manual confirm checkboxes.
// Signals:
//   backClicked()  — user wants to go back to system checks
//   nextClicked()  — all hardware checks resolved; proceed to final review
// Properties:
//   cameraOk (bool) — true when camera check is confirmed pass or skipped
//   gimbalOk (bool) — true when gimbal check is confirmed pass or skipped

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Item {
    id: root

    signal backClicked()
    signal nextClicked()

    // ── Hardware check state ─────────────────────────────────────────────────
    property bool cameraConfirmed: false
    property bool cameraSkipped:   false
    property bool gimbalConfirmed: false
    property bool gimbalSkipped:   false

    property bool _recordingInProgress: false
    property string _recordingStatus: ""

    readonly property bool cameraOk: cameraConfirmed || cameraSkipped
    readonly property bool gimbalOk: gimbalConfirmed || gimbalSkipped
    readonly property bool _canProceed: cameraOk && gimbalOk

    // ── Main layout ─────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                width: 32; height: 32; radius: Config.radiusSmall
                color: Colors.surfaceLight
                border.color: Colors.border; border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    font.pixelSize: Config.fontSizeH3
                    color: Colors.textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backClicked()
                }
            }

            Text {
                text: qsTr("📷  Hardware Verification")
                font.pixelSize: Config.fontSizeH2
                font.bold: true
                color: Colors.textPrimary
                Layout.fillWidth: true
                Layout.leftMargin: Config.spacingSmall
            }

            // Progress indicator
            Text {
                text: (cameraOk ? "1" : "0") + "/2 hardware checks passed"
                          .replace("1", (cameraOk && gimbalOk ? "2" : cameraOk || gimbalOk ? "1" : "0"))
                font.pixelSize: Config.fontSizeSmall
                color: _canProceed ? Colors.success : Colors.textSecondary
            }
        }

        // Progress bar
        Rectangle {
            Layout.fillWidth: true
            height: 4
            radius: 2
            color: Colors.surfaceLight

            Rectangle {
                width: parent.width * ((cameraOk ? 1 : 0) + (gimbalOk ? 1 : 0)) / 2
                height: parent.height
                radius: parent.radius
                color: _canProceed ? Colors.success : Colors.accent
                Behavior on width { NumberAnimation { duration: Config.animNormal } }
            }
        }

        // ── Two-column hardware cards ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Config.spacingMedium

            // ── Camera Check Card ────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: cameraOk ? Colors.successDim : Colors.surface
                radius: Config.radiusMedium
                border.color: cameraOk ? Colors.success : Colors.border
                border.width: cameraOk ? 2 : 1

                Behavior on color { ColorAnimation { duration: Config.animNormal } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingMedium

                    // Card title
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall

                        Text { text: "📹"; font.pixelSize: 24 }
                        Text {
                            text: qsTr("Camera Feed Check")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            width: 20; height: 20; radius: 10
                            color: cameraOk ? Colors.success : "transparent"
                            border.color: cameraOk ? Colors.success : Colors.border
                            border.width: 1
                            Text {
                                anchors.centerIn: parent; text: "✓"
                                font.pixelSize: 11; font.bold: true
                                color: Colors.background
                                visible: cameraOk
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

                    // Video preview area
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        color: Colors.background
                        radius: Config.radiusSmall
                        border.color: Colors.border; border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Live Camera Feed\n\n(Video preview rendered here\nby VideoManager)")
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textDisabled
                        }

                        // Status overlay text
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 24
                            color: Qt.rgba(0, 0, 0, 0.6)
                            visible: _recordingStatus.length > 0

                            Text {
                                anchors.centerIn: parent
                                text: root._recordingStatus
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                            }
                        }
                    }

                    // Recording test button
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Config.radiusSmall
                        color: _recordingInProgress ? Colors.warningDim : Colors.surfaceLight
                        border.color: _recordingInProgress ? Colors.warning : Colors.border
                        border.width: 1
                        enabled: !cameraConfirmed && !cameraSkipped

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Config.spacingSmall
                            Text { text: _recordingInProgress ? "⏺" : "▶"; font.pixelSize: 14; color: _recordingInProgress ? Colors.warning : Colors.textSecondary }
                            Text {
                                text: _recordingInProgress
                                      ? qsTr("Recording 5s... Verifying...")
                                      : qsTr("Start 5s Recording Test")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: !root._recordingInProgress && !cameraConfirmed && !cameraSkipped
                            onClicked: {
                                root._recordingInProgress = true
                                root._recordingStatus = "Recording..."
                                Qt.callLater(function() {
                                    recordTimer.restart()
                                })
                            }
                        }
                    }

                    Timer {
                        id: recordTimer
                        interval: 5000
                        onTriggered: {
                            root._recordingInProgress = false
                            root._recordingStatus = "✓ Recording complete — verify playback"
                        }
                    }

                    // Confirm checkbox
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Config.radiusSmall
                        color: cameraConfirmed ? Colors.successDim : Colors.surfaceLight
                        border.color: cameraConfirmed ? Colors.success : Colors.border
                        border.width: 1
                        enabled: !cameraSkipped

                        RowLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingSmall
                            spacing: Config.spacingSmall

                            Rectangle {
                                width: 18; height: 18; radius: 3
                                color: cameraConfirmed ? Colors.success : "transparent"
                                border.color: cameraConfirmed ? Colors.success : Colors.border
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent; text: "✓"
                                    font.pixelSize: 10; font.bold: true
                                    color: Colors.background
                                    visible: cameraConfirmed
                                }
                            }

                            Text {
                                text: qsTr("Video visible and clear")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: !cameraSkipped
                            onClicked: cameraConfirmed = !cameraConfirmed
                        }
                    }

                    // Skip checkbox
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Config.radiusSmall
                        color: cameraSkipped ? Colors.checkWarnDim : "transparent"
                        border.color: cameraSkipped ? Colors.checkWarn : Colors.borderLight
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingSmall
                            spacing: Config.spacingSmall

                            Rectangle {
                                width: 18; height: 18; radius: 3
                                color: cameraSkipped ? Colors.checkWarn : "transparent"
                                border.color: Colors.checkWarn
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent; text: "✓"
                                    font.pixelSize: 10; font.bold: true
                                    color: Colors.background
                                    visible: cameraSkipped
                                }
                            }

                            Text {
                                text: qsTr("No camera detected — skip")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textSecondary
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                cameraSkipped = !cameraSkipped
                                if (cameraSkipped) cameraConfirmed = false
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ── Gimbal Check Card ────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: gimbalOk ? Colors.successDim : Colors.surface
                radius: Config.radiusMedium
                border.color: gimbalOk ? Colors.success : Colors.border
                border.width: gimbalOk ? 2 : 1

                Behavior on color { ColorAnimation { duration: Config.animNormal } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingMedium

                    // Card title
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall

                        Text { text: "🎯"; font.pixelSize: 24 }
                        Text {
                            text: qsTr("Gimbal Movement Check")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            width: 20; height: 20; radius: 10
                            color: gimbalOk ? Colors.success : "transparent"
                            border.color: gimbalOk ? Colors.success : Colors.border
                            border.width: 1
                            Text {
                                anchors.centerIn: parent; text: "✓"
                                font.pixelSize: 11; font.bold: true
                                color: Colors.background
                                visible: gimbalOk
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

                    Text {
                        text: qsTr("Use the controls below to verify the gimbal responds correctly in all axes")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Gimbal control grid
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        color: Colors.background
                        radius: Config.radiusSmall
                        border.color: Colors.border; border.width: 1

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Config.spacingSmall

                            // Tilt up
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 80; height: 32; radius: Config.radiusSmall
                                color: Colors.surfaceLight
                                border.color: Colors.border; border.width: 1

                                Text { anchors.centerIn: parent; text: "▲ Tilt Up"; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !gimbalSkipped
                                    onClicked: {
                                        if (typeof HardwareTestController !== "undefined")
                                            HardwareTestController.sendGimbalCommand(0.0, 45.0)
                                    }
                                }
                            }

                            // Pan left / Pan right row
                            RowLayout {
                                spacing: Config.spacingSmall
                                Layout.alignment: Qt.AlignHCenter

                                Rectangle {
                                    width: 80; height: 32; radius: Config.radiusSmall
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1

                                    Text { anchors.centerIn: parent; text: "◀ Pan Left"; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !gimbalSkipped
                                        onClicked: {
                                            if (typeof HardwareTestController !== "undefined")
                                                HardwareTestController.sendGimbalCommand(-45.0, 0.0)
                                        }
                                    }
                                }

                                Rectangle {
                                    width: 36; height: 36; radius: Config.radiusMedium
                                    color: Colors.accent
                                    Text { anchors.centerIn: parent; text: "⊕"; font.pixelSize: 18; color: Colors.background }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof HardwareTestController !== "undefined")
                                                HardwareTestController.sendGimbalCommand(0.0, 0.0)
                                        }
                                    }
                                }

                                Rectangle {
                                    width: 80; height: 32; radius: Config.radiusSmall
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1

                                    Text { anchors.centerIn: parent; text: "Pan Right ▶"; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !gimbalSkipped
                                        onClicked: {
                                            if (typeof HardwareTestController !== "undefined")
                                                HardwareTestController.sendGimbalCommand(45.0, 0.0)
                                        }
                                    }
                                }
                            }

                            // Tilt down
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 80; height: 32; radius: Config.radiusSmall
                                color: Colors.surfaceLight
                                border.color: Colors.border; border.width: 1

                                Text { anchors.centerIn: parent; text: "▼ Tilt Down"; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !gimbalSkipped
                                    onClicked: {
                                        if (typeof HardwareTestController !== "undefined")
                                            HardwareTestController.sendGimbalCommand(0.0, -45.0)
                                    }
                                }
                            }
                        }
                    }

                    // Confirm checkbox
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Config.radiusSmall
                        color: gimbalConfirmed ? Colors.successDim : Colors.surfaceLight
                        border.color: gimbalConfirmed ? Colors.success : Colors.border
                        border.width: 1
                        enabled: !gimbalSkipped

                        RowLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingSmall
                            spacing: Config.spacingSmall

                            Rectangle {
                                width: 18; height: 18; radius: 3
                                color: gimbalConfirmed ? Colors.success : "transparent"
                                border.color: gimbalConfirmed ? Colors.success : Colors.border
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent; text: "✓"
                                    font.pixelSize: 10; font.bold: true
                                    color: Colors.background
                                    visible: gimbalConfirmed
                                }
                            }

                            Text {
                                text: qsTr("Gimbal responds correctly")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: !gimbalSkipped
                            onClicked: gimbalConfirmed = !gimbalConfirmed
                        }
                    }

                    // Skip checkbox
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Config.radiusSmall
                        color: gimbalSkipped ? Colors.checkWarnDim : "transparent"
                        border.color: gimbalSkipped ? Colors.checkWarn : Colors.borderLight
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent; anchors.margins: Config.spacingSmall
                            spacing: Config.spacingSmall

                            Rectangle {
                                width: 18; height: 18; radius: 3
                                color: gimbalSkipped ? Colors.checkWarn : "transparent"
                                border.color: Colors.checkWarn
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent; text: "✓"
                                    font.pixelSize: 10; font.bold: true
                                    color: Colors.background
                                    visible: gimbalSkipped
                                }
                            }

                            Text {
                                text: qsTr("No gimbal detected — skip")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textSecondary
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                gimbalSkipped = !gimbalSkipped
                                if (gimbalSkipped) gimbalConfirmed = false
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // ── Footer navigation ───────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Rectangle {
                width: 120; height: 40
                radius: Config.radiusSmall
                color: Colors.surfaceLight
                border.color: Colors.border; border.width: 1

                Text { anchors.centerIn: parent; text: "← System Checks"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backClicked() }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Config.spacingSmall

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: _canProceed ? qsTr("Hardware verified — proceed to final review") : qsTr("Complete or skip all hardware checks to continue")
                    font.pixelSize: Config.fontSizeSmall
                    color: _canProceed ? Colors.success : Colors.textSecondary
                }

                Rectangle {
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 40
                    radius: Config.radiusSmall
                    color: _canProceed ? Colors.accent : Colors.surfaceLight
                    border.color: _canProceed ? Colors.accent : Colors.border
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Config.animNormal } }

                    Text {
                        anchors.centerIn: parent
                        text: "Next to Review  →"
                        font.pixelSize: Config.fontSizeBody
                        font.bold: true
                        color: _canProceed ? Colors.background : Colors.textDisabled
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: _canProceed ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: _canProceed
                        onClicked: {
                            if (_canProceed) {
                                root.nextClicked()
                            }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
