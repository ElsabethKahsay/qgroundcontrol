import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.ScreenTools

import com.uav.preflight 1.0

Dialog {
    id: root

    title: qsTr("Select Session Mode")
    modal: true
    x: Math.round((Overlay.overlay ? Overlay.overlay.width : parent.width) / 2 - width / 2)
    y: Math.round((Overlay.overlay ? Overlay.overlay.height : parent.height) / 2 - height / 2)
    width: Math.min(540, (Overlay.overlay ? Overlay.overlay.width : parent.width) * 0.95)
    closePolicy: Popup.NoAutoClose
    focus: true
    padding: Config.spacingLarge

    background: Rectangle {
        color: Colors.surface
        border.color: Colors.border
        border.width: 1
        radius: Config.radiusMedium
    }

    header: Rectangle {
        color: Colors.surfaceLight
        radius: Config.radiusMedium
        height: 48
        width: parent.width

        Text {
            anchors.centerIn: parent
            text: qsTr("\uD83D\uDEEB  Select Session Mode")
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Config.spacingMedium

        // ── Mode tiles ─────────────────────────────────────────────
        RowLayout {
            spacing: Config.spacingMedium
            Layout.fillWidth: true

            // Training tile
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                radius: Config.radiusLarge
                color: Colors.surfaceLight
                border.color: Colors.accent
                border.width: 2

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Config.spacingSmall

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("\uD83C\uDF93")
                        font.pixelSize: 32
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Training")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Dual RC Stored")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        FlightSession.setMode("TRAINING")
                        root.close()
                    }
                }
            }

            // Testing tile
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                radius: Config.radiusLarge
                color: Colors.surfaceLight
                border.color: Colors.accentCyan
                border.width: 2

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Config.spacingSmall

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("\u2699\uFE0F")
                        font.pixelSize: 32
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Testing")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("No audit logs")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        FlightSession.setMode("TESTING")
                        root.close()
                    }
                }
            }

            // Flight tile
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                radius: Config.radiusLarge
                color: Colors.surfaceLight
                border.color: Colors.success
                border.width: 2

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Config.spacingSmall

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("\u2708\uFE0F")
                        font.pixelSize: 32
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Flight")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Full audit")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        FlightSession.setMode("FLIGHT")
                        root.close()
                    }
                }
            }
        }
    }
}
