// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: qml/pages/TemplateManager.qml
// Description: Allows users to manage checklist templates.

import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Page {
    background: Rectangle { color: Colors.background }

    property var templates: []

    function reloadTemplates() {
        var dbTemplates = Database.listTemplates("Quad") // Fetching Quad by default for demo
        var arr = []
        for (var i = 0; i < dbTemplates.length; i++) {
            arr.push(dbTemplates[i])
        }
        if (arr.length === 0) {
            arr.push("quad_standard (Built-in)")
        }
        templates = arr
    }

    Component.onCompleted: reloadTemplates()

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        Text {
            text: "Template Manager"
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        ListView {
            width: parent.width
            height: parent.height - 180
            model: templates
            clip: true
            spacing: Config.spacingSmall

            delegate: Rectangle {
                width: parent.width
                height: 60
                color: Colors.surface
                radius: Config.radiusSmall
                border.color: Colors.border

                Row {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingMedium

                    Text {
                        text: modelData
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 100
                    }

                    CustomButton {
                        text: "Edit"
                        width: 80
                        height: 36
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: Window.window.mainStackView.push("TemplateEditor.qml", { templateId: modelData })
                    }
                }
            }
        }

        Row {
            spacing: Config.spacingMedium
            anchors.horizontalCenter: parent.horizontalCenter

            CustomButton {
                text: "← Back"
                width: (parent.parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 2
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.pop()
            }
            CustomButton {
                text: "Create New"
                width: (parent.parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 2
                onClicked: Window.window.mainStackView.push("TemplateEditor.qml", { templateId: "new_template" })
            }
        }
    }
}
