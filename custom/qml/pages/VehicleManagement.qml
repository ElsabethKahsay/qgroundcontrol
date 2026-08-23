// SPDX-License-Identifier: GPL-3.0-or-later
// Vehicle management page — list registered vehicles, rename, view history

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    background: Rectangle { color: Colors.background }

    property var vehiclesModel: []

    function refreshVehicles() {
        var json = Database.getAllVehiclesJson()
        vehiclesModel = JSON.parse(json)
    }

    Component.onCompleted: refreshVehicles()

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        Text {
            text: qsTr("Registered Vehicles")
            font.pixelSize: Config.fontSizeH2
            color: Colors.secondary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: vehiclesModel.length + " vehicle(s) registered"
            font.pixelSize: Config.fontSizeBody
            color: Colors.textSecondary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        CustomButton {
            text: qsTr("↻ Refresh")
            anchors.horizontalCenter: parent.horizontalCenter
            width: 120
            height: 28
            baseColor: Colors.surface
            onClicked: refreshVehicles()
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Colors.border
        }

        ScrollView {
            width: parent.width
            height: parent.height - y - Config.spacingLarge
            contentWidth: availableWidth
            clip: true

            Column {
                width: parent.width
                spacing: Config.spacingSmall

                Rectangle {
                    id: profileBox
                    width: parent.width
                    height: 52
                    color: Colors.surface
                    radius: 4
                    border.color: Colors.border
                    border.width: 1
                    property double _pay: VehicleProfileManager ? VehicleProfileManager.currentPayloadWeightKg : 0

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Config.spacingMedium; anchors.rightMargin: Config.spacingMedium
                        spacing: Config.spacingSmall

                        Text { text: "Payload (kg)"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary; font.bold: true }
                        Rectangle {
                            Layout.preferredWidth: 90; Layout.preferredHeight: 32
                            radius: 3
                            border.color: Colors.border; border.width: 1
                            color: Colors.surface
                            TextInput {
                                anchors.fill: parent; anchors.margins: 4
                                font.pixelSize: Config.fontSizeBody; font.family: "monospace"
                                horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
                                color: Colors.textPrimary
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                text: profileBox._pay !== undefined ? profileBox._pay.toFixed(1) : "0.0"
                                onEditingFinished: {
                                    var v = parseFloat(text)
                                    if (!isNaN(v) && v >= 0) {
                                        VehicleProfileManager.setPayloadWeightKg(v)
                                        profileBox._pay = v
                                    } else if (profileBox) {
                                        profileBox._pay = VehicleProfileManager ? VehicleProfileManager.currentPayloadWeightKg : 0
                                    }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Connections {
                            target: VehicleProfileManager
                            function onPayloadWeightChanged() { if (profileBox) profileBox._pay = VehicleProfileManager.currentPayloadWeightKg }
                        }
                    }
                }

                Repeater {
                    model: vehiclesModel

                    delegate: Rectangle {
                        width: parent.width
                        height: Math.max(110, row.implicitHeight + Config.spacingMedium * 2)
                        color: Colors.surface
                        radius: Config.radiusMedium
                        border.color: Colors.border
                        border.width: 1

                        property bool editing: false
                        property string editName: modelData.friendlyName || ""

                        Row {
                            id: row
                            anchors.fill: parent
                            anchors.margins: Config.spacingMedium
                            spacing: Config.spacingMedium

                            Column {
                                width: parent.width - renameBtn.width - Config.spacingMedium * 2
                                spacing: 6

                                Text {
                                    text: modelData.friendlyName || "unnamed"
                                    font.pixelSize: Config.fontSizeH3
                                    font.bold: true
                                    color: Colors.textPrimary
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: modelData.autopilotType + " · " + modelData.airframeType
                                           + " · v" + modelData.firmwareVersion
                                    font.pixelSize: Config.fontSizeBody
                                    color: Colors.textSecondary
                                }

                                Text {
                                    text: "Flights: " + modelData.totalFlightCount
                                           + " · Hours: " + modelData.totalFlightHours.toFixed(1)
                                    font.pixelSize: Config.fontSizeBody
                                    color: Colors.textSecondary
                                }

                                Text {
                                    text: "Last seen: " + (modelData.lastSeen || "never")
                                    font.pixelSize: Config.fontSizeBody
                                    color: Colors.textMuted
                                }

                                Text {
                                    text: "UID: " + modelData.deviceUid
                                    font.pixelSize: Config.fontSizeSmall
                                    color: Colors.textDisabled
                                }
                            }

                            Column {
                                id: renameBtn
                                spacing: Config.spacingSmall
                                anchors.verticalCenter: parent.verticalCenter

                                CustomButton {
                                    text: editing ? "Save" : "Rename"
                                    width: 90
                                    height: 34
                                    baseColor: Colors.surface
                                    onClicked: {
                                        if (editing) {
                                            if (editName.length > 0 && editName !== modelData.friendlyName) {
                                                Database.updateVehicleName(modelData.fingerprint, editName)
                                                refreshVehicles()
                                            }
                                            editing = false
                                        } else {
                                            editing = true
                                            editName = modelData.friendlyName || ""
                                        }
                                    }
                                }

                                CustomButton {
                                    text: qsTr("History")
                                    width: 90
                                    height: 34
                                    baseColor: Colors.surface
                                    onClicked: {
                                        // Show flight sessions for this vehicle
                                        var sessions = JSON.parse(Database.getFlightSessions(modelData.fingerprint, 10))
                                        var msg = "Flight Sessions:\n"
                                        for (var i = 0; i < sessions.length; i++) {
                                            var s = sessions[i]
                                            msg += "  #" + s.id + " — " + (s.endedAt ? "completed" : "in progress")
                                            if (s.durationSeconds > 0)
                                                msg += " (" + (s.durationSeconds / 60).toFixed(1) + " min)"
                                            msg += "\n"
                                        }
                                        if (sessions.length === 0) msg = "No flight sessions recorded."
                                        historyDialog.text = msg
                                        historyDialog.open()
                                    }
                                }
                            }
                        }

                        TextField {
                            visible: editing
                            anchors.top: parent.top
                            anchors.topMargin: Config.spacingMedium
                            anchors.left: parent.left
                            anchors.leftMargin: Config.spacingMedium
                            anchors.right: parent.right
                            anchors.rightMargin: Config.spacingMedium + 70
                            text: editName
                            color: Colors.textPrimary
                            background: Rectangle {
                                color: Colors.surface
                                radius: Config.radiusSmall
                                border.color: Colors.primary
                            }
                            onTextChanged: editName = text
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: historyDialog
        property string text: ""
        title: qsTr("Flight History")
        standardButtons: Dialog.Ok
        modal: true

        TextArea {
            text: historyDialog.text
            readOnly: true
            width: 300
            height: 200
        }
    }
}
