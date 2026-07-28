// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: qml/cpts/VideoPreview.qml
// Description: Component to show the video stream and trigger gimbal tests.

import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Rectangle {
    id: root
    width: parent ? parent.width : 360
    height: 240
    color: "#1a1a1a"
    radius: Config.radiusMedium
    border.color: Colors.border
    border.width: 1

    property bool linkActive: VideoManager.videoLinkActive

    Column {
        anchors.centerIn: parent
        spacing: Config.spacingMedium
        visible: !linkActive

        Text {
            text: qsTr("No Video Stream")
            color: Colors.textSecondary
            font.pixelSize: Config.fontSizeBody
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // Mock video stream (since QtMultimedia wasn't included)
    Item {
        anchors.fill: parent
        anchors.margins: 2
        visible: linkActive
        clip: true

        Rectangle {
            anchors.fill: parent
            color: "#0f0f0f"
            radius: Config.radiusMedium - 2
        }

        // Animated noise/grid to simulate video
        Grid {
            anchors.centerIn: parent
            columns: 10
            spacing: 2
            Repeater {
                model: 50
                Rectangle {
                    width: 20
                    height: 20
                    color: Qt.rgba(Math.random()*0.5, Math.random()*0.5, 0.8, 0.3)
                    
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.1; duration: 500 + Math.random() * 1000 }
                        NumberAnimation { to: 0.8; duration: 500 + Math.random() * 1000 }
                    }
                }
            }
        }

        Text {
            text: "Stream: " + VideoManager.videoStreamUrl
            color: Colors.primary
            font.pixelSize: Config.fontSizeSmall
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: Config.spacingSmall
        }
        
        Text {
            text: VideoManager.isRecording ? "REC" : ""
            color: Colors.error
            font.pixelSize: Config.fontSizeBody
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Config.spacingSmall
        }
    }

    // Controls overlay
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: Config.spacingSmall
        spacing: Config.spacingSmall
        visible: linkActive

        Button {
            text: qsTr("Test Gimbal")
            onClicked: {
                VideoManager.startGimbalTest()
                if (Window.window && Window.window.mainStackView) {
                    Window.window.mainStackView.push(Qt.resolvedUrl("../pages/GimbalTest.qml"))
                }
            }
            height: 30
            background: Rectangle {
                color: Colors.surface
                radius: Config.radiusSmall
            }
            contentItem: Text {
                text: parent.text
                color: Colors.textPrimary
                font.pixelSize: Config.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Button {
            text: VideoManager.isRecording ? "Stop REC" : "Start REC"
            onClicked: {
                if (VideoManager.isRecording) {
                    VideoManager.stopRecording()
                } else {
                    VideoManager.startRecording()
                }
            }
            height: 30
            background: Rectangle {
                color: VideoManager.isRecording ? Colors.surface : Colors.error
                radius: Config.radiusSmall
            }
            contentItem: Text {
                text: parent.text
                color: Colors.textPrimary
                font.pixelSize: Config.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
