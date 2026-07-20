// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: qml/pages/TemplateEditor.qml
// Description: Editor to create and modify checklist templates.

import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Page {
    background: Rectangle { color: Colors.background }

    property string templateId: "new_template"
    property var templateItems: []

    function loadData() {
        if (templateId !== "new_template") {
            var items = TemplateManager.loadTemplate(templateId)
            if (items.length > 0) {
                templateItems = items
                return
            }
        }
        
        // Default empty state
        templateItems = [
            { "id": "t.item1", "text": "New Item", "type": "manual", "isMandatory": true }
        ]
    }

    Component.onCompleted: loadData()

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        Text {
            text: templateId === "new_template" ? "Create Template" : "Edit Template"
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Template Name")
            text: templateId !== "new_template" ? templateId : ""
            font.pixelSize: Config.fontSizeBody
            color: Colors.textPrimary
            background: Rectangle {
                color: Colors.surface
                radius: Config.radiusSmall
                border.color: Colors.border
            }
        }

        ListView {
            width: parent.width
            height: parent.height - 240
            model: templateItems
            clip: true
            spacing: Config.spacingSmall

            delegate: Rectangle {
                width: parent.width
                height: 80
                color: Colors.surface
                radius: Config.radiusSmall
                border.color: Colors.border

                Column {
                    anchors.fill: parent
                    anchors.margins: Config.spacingSmall
                    spacing: 4

                    Row {
                        spacing: Config.spacingSmall
                        TextField {
                            text: modelData.text
                            width: parent.width - 100
                            font.pixelSize: Config.fontSizeBody
                            color: Colors.textPrimary
                            background: Rectangle { color: "transparent" }
                            onTextChanged: {
                                var temp = templateItems.slice()
                                temp[index].text = text
                                templateItems = temp
                            }
                        }
                        
                        Text {
                            text: modelData.type.toUpperCase()
                            color: Colors.secondary
                            font.pixelSize: Config.fontSizeSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        text: "ID: " + modelData.id
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }
                }
            }
        }

        Row {
            spacing: Config.spacingMedium
            anchors.horizontalCenter: parent.horizontalCenter

            CustomButton {
                text: qsTr("Add Item")
                width: (parent.parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                onClicked: {
                    var temp = templateItems.slice()
                    temp.push({ "id": "t.item" + (temp.length+1), "text": "New Item", "type": "manual", "isMandatory": true })
                    templateItems = temp
                }
            }

            CustomButton {
                text: qsTr("Save")
                width: (parent.parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                onClicked: {
                    var tName = nameField.text !== "" ? nameField.text : "untitled_template"
                    var success = TemplateManager.saveTemplate(tName, "Quad", tName, templateItems)
                    if (success) {
                        Window.window.mainStackView.pop()
                    }
                }
            }
            
            CustomButton {
                text: qsTr("Cancel")
                width: (parent.parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.pop()
            }
        }
    }
}
