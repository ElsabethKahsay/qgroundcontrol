// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: qml/cpts/AlertPanel.qml
// Description: Slide-in panel displaying active system alerts with severity styling.

import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Rectangle {
    id: root
    width: 350
    height: parent.height
    x: parent.width  // Start off-screen right
    color: Colors.surface
    border.color: Colors.border
    border.width: 1

    property bool isOpen: false

    Behavior on x {
        NumberAnimation { duration: Config.animationDuration }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        Text {
            text: "Alerts (" + AlertManager.warningCount + "W, " + 
                  AlertManager.cautionCount + "C, " + AlertManager.advisoryCount + "A)"
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
        }

        ListView {
            width: parent.width
            height: parent.height - 100
            model: AlertManager
            clip: true
            spacing: Config.spacingSmall

            delegate: Rectangle {
                width: parent.width
                height: 80
                radius: Config.radiusSmall
                
                color: {
                    switch (model.severity) {
                    case 0: return Colors.surfaceLight  // Advisory
                    case 1: return Qt.rgba(1, 0.72, 0.4, 0.1)  // Caution
                    case 2: return Qt.rgba(1, 0.33, 0.33, 0.1)  // Warning
                    default: return Colors.surface
                    }
                }
                
                border.color: {
                    switch (model.severity) {
                    case 0: return Colors.primary
                    case 1: return Colors.warning
                    case 2: return Colors.error
                    default: return Colors.border
                    }
                }
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: Config.spacingSmall
                    spacing: 2

                    Row {
                        spacing: Config.spacingSmall
                        Text {
                            text: {
                                switch (model.severity) {
                                case 0: return "ℹ️ ADVISORY"
                                case 1: return "⚠️ CAUTION"
                                case 2: return "🚨 WARNING"
                                default: return "?"
                                }
                            }
                            font.pixelSize: Config.fontSizeSmall
                            color: {
                                switch (model.severity) {
                                case 0: return Colors.primary
                                case 1: return Colors.warning
                                case 2: return Colors.error
                                default: return Colors.textSecondary
                                }
                            }
                        }
                        Text {
                            text: Qt.formatDateTime(model.timestamp, "hh:mm:ss")
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                        }
                    }

                    Text {
                        text: model.message
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textPrimary
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        AlertManager.acknowledgeAlert(model.id)
                    }
                }
                
                // Dim if acknowledged
                opacity: model.acknowledged ? 0.5 : 1.0
            }
        }

        CustomButton {
            text: "Close Alerts"
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            onClicked: root.close()
        }
    }

    function open() {
        isOpen = true
        x = parent.width - width
    }

    function close() {
        isOpen = false
        x = parent.width
    }
}
