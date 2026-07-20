import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.Controls
import com.uav.preflight 1.0

AnalyzePage {
    id:                 root
    pageName:           qsTr("Vehicles")
    pageDescription:    qsTr("Previously connected vehicles")

    property var vehiclesModel: []

    function refresh() {
        vehiclesModel = JSON.parse(Database.getAllVehiclesJson())
    }

    Component.onCompleted: refresh()

    pageComponent: Component {
        ColumnLayout {
            width:  root.width
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: vehiclesModel.length + " vehicle(s)"
                    font.pixelSize: 14
                    color: Colors.textSecondary
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("↻ Refresh")
                    onClicked: refresh()
                    flat: true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Colors.divider
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: vehiclesModel

                        delegate: Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: 90
                            color: Colors.bgPrimary
                            radius: 6
                            border.color: Colors.divider
                            border.width: 1

                            property bool _editing: false
                            property string _editName: modelData.friendlyName || ""

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Text {
                                        text: modelData.friendlyName || "unnamed"
                                        font.pixelSize: 15
                                        font.bold: true
                                        color: Colors.textPrimary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.autopilotType + " \\u00b7 " + modelData.airframeType
                                               + " \\u00b7 v" + modelData.firmwareVersion
                                        font.pixelSize: 12
                                        color: Colors.textSecondary
                                    }

                                    Text {
                                        text: "Flights: " + modelData.totalFlightCount
                                               + " \\u00b7 Hours: " + Number(modelData.totalFlightHours).toFixed(1)
                                        font.pixelSize: 12
                                        color: Colors.textSecondary
                                    }

                                    Text {
                                        text: "Last seen: " + (modelData.lastSeen || "never")
                                        font.pixelSize: 11
                                        color: Colors.textMuted
                                    }
                                }

                                ColumnLayout {
                                    spacing: 4

                                    Button {
                                        text: _editing ? "Save" : "Rename"
                                        onClicked: {
                                            if (_editing) {
                                                if (_editName.length > 0 && _editName !== modelData.friendlyName) {
                                                    Database.updateVehicleName(modelData.fingerprint, _editName)
                                                    refresh()
                                                }
                                                _editing = false
                                            } else {
                                                _editing = true
                                                _editName = modelData.friendlyName || ""
                                            }
                                        }
                                    }

                                    Button {
                                        text: qsTr("UID")
                                        onClicked: {
                                            uidDialog.fp = modelData.fingerprint || "N/A"
                                            uidDialog.uid = modelData.deviceUid || "N/A"
                                            uidDialog.brd = modelData.boardVersion || "N/A"
                                            uidDialog.cid = String(modelData.compid || "N/A")
                                            uidDialog.open()
                                        }
                                    }
                                }
                            }

                            Dialog {
                                id: uidDialog
                                title: qsTr("Vehicle Details")
                                standardButtons: Dialog.Ok
                                modal: true
                                x: Math.round((parent.width - width) / 2)
                                y: Math.round((parent.height - height) / 2)
                                width: 320

                                property string fp: ""
                                property string uid: ""
                                property string brd: ""
                                property string cid: ""

                                ColumnLayout {
                                    spacing: 6
                                    Text { text: "Fingerprint: " + uidDialog.fp; color: Colors.textSecondary }
                                    Text { text: "UID: " + uidDialog.uid; color: Colors.textSecondary }
                                    Text { text: "Board: " + uidDialog.brd; color: Colors.textSecondary }
                                    Text { text: "COMPID: " + uidDialog.cid; color: Colors.textSecondary }
                                }
                            }

                            TextField {
                                visible: _editing
                                anchors.top: parent.top
                                anchors.topMargin: 10
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                text: _editName
                                color: Colors.textPrimary
                                background: Rectangle {
                                    color: Colors.surface
                                    radius: 4
                                    border.color: Colors.info
                                }
                                onTextChanged: _editName = text
                            }
                        }
                    }
                }
            }
        }
    }
}
