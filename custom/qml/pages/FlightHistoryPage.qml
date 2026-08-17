import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.Controls
import com.uav.preflight 1.0

AnalyzePage {
    id: root
    pageName: qsTr("Flight History")
    pageDescription: qsTr("Completed flight records")

    property var _model: FlightHistoryModel {}

    Component.onCompleted: {
        var vid = VehicleRegistry.currentVehicleId
        if (vid > 0) _model.loadForVehicle(vid)
    }

    pageComponent: Component {
        RowLayout {
            width: root.availableWidth
            height: root.availableHeight
            spacing: 0

            // ── Left: flight list ─────────────────────────────────
            Rectangle {
                Layout.preferredWidth: parent.width * 0.35
                Layout.fillHeight: true
                color: Colors.surface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingSmall

                    Text {
                        text: qsTr("Flights")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Colors.divider
                    }

                    ListView {
                        id: flightList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: _model
                        clip: true
                        spacing: 4

                        delegate: Rectangle {
                            width: flightList.width
                            height: 56
                            radius: Config.radiusSmall
                            color: ListView.isCurrentItem ? Colors.surfaceLight : "transparent"
                            border.color: ListView.isCurrentItem ? Colors.accent : "transparent"
                            border.width: ListView.isCurrentItem ? 1 : 0

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    flightList.currentIndex = index
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Config.spacingSmall
                                spacing: Config.spacingSmall

                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.fillHeight: true
                                    radius: Config.radiusSmall
                                    color: model.mode === "TRAINING" ? Colors.accent : Colors.success

                                    Text {
                                        anchors.centerIn: parent
                                        text: model.mode === "TRAINING" ? "T" : "F"
                                        font.bold: true
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textInverse
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: model.date
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                    }

                                    Text {
                                        text: model.operatorName
                                        font.pixelSize: Config.fontSizeBody
                                        font.bold: true
                                        color: Colors.textPrimary
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: model.purpose
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: model.duration + "s"
                                    font.pixelSize: Config.fontSizeSmall
                                    color: Colors.textSecondary
                                }
                            }
                        }
                    }
                }
            }

            // ── Right: flight detail ──────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Colors.surfaceLight

                Item {
                    anchors.fill: parent
                    anchors.margins: Config.spacingLarge

                    Text {
                        anchors.centerIn: parent
                        visible: flightList.currentIndex < 0
                        text: qsTr("Select a flight from the list")
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textSecondary
                    }

                    // Detail for selected flight
                    ScrollView {
                        visible: flightList.currentIndex >= 0
                        anchors.fill: parent
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: Config.spacingMedium

                            Text {
                                text: qsTr("Flight Details")
                                font.pixelSize: Config.fontSizeH3
                                font.bold: true
                                color: Colors.textPrimary
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Colors.divider
                            }

                            Text {
                                text: qsTr("Vehicle: %1").arg(VehicleRegistry.currentVehicleName)
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                text: qsTr("Mode: %1").arg(_model.getFlightData(_model.data(
                                    _model.index(flightList.currentIndex, 0),
                                    FlightHistoryModel.FlightIdRole)).value("mode").toString())
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                text: qsTr("Purpose: %1").arg(_model.data(
                                    _model.index(flightList.currentIndex, 0),
                                    FlightHistoryModel.PurposeRole))
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                text: qsTr("Location: %1").arg(_model.data(
                                    _model.index(flightList.currentIndex, 0),
                                    FlightHistoryModel.LocationRole))
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                text: qsTr("Duration: %1 sec").arg(_model.data(
                                    _model.index(flightList.currentIndex, 0),
                                    FlightHistoryModel.DurationRole).toString())
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }

                            Text {
                                text: qsTr("Operator: %1").arg(_model.data(
                                    _model.index(flightList.currentIndex, 0),
                                    FlightHistoryModel.OperatorNameRole))
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                            }
                        }
                    }
                }
            }
        }
    }
}
