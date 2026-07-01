import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtPositioning
import QtLocation
import com.uav.mission 1.0

Dialog {
    id: root

    property var missionStub: null
    property var waypointMath: WaypointMath {}

    readonly property var previewCoord: {
        if (!missionStub) return null
        var ref = refCombo.currentIndex === 0
            ? missionStub.aircraftPosition
            : (missionStub.waypoints.length > 0 ? missionStub.waypoints[missionStub.waypoints.length - 1] : missionStub.aircraftPosition)
        if (!ref || !ref.isValid) return null
        return waypointMath.coordinateFromBearingAndDistance(ref, bearingSpin.value, distanceSpin.value)
    }

    readonly property double sanityBearingValue: {
        if (!previewCoord || !missionStub) return -1
        var ref = refCombo.currentIndex === 0
            ? missionStub.aircraftPosition
            : (missionStub.waypoints.length > 0 ? missionStub.waypoints[missionStub.waypoints.length - 1] : missionStub.aircraftPosition)
        if (!ref || !ref.isValid) return -1
        return waypointMath.bearingBetweenCoordinates(previewCoord, ref)
    }

    title: "Add Waypoint by Bearing & Distance"
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent.width * 0.92)
    closePolicy: Popup.CloseOnEscape
    padding: 16

    background: Rectangle {
        color: "#1A0A2E"
        border.color: "#E91E63"
        border.width: 2
        radius: 8
    }

    header: Rectangle {
        color: "#2D1B4E"
        radius: 8
        height: 48
        width: parent.width
        RowLayout {
            anchors.fill: parent; anchors.margins: 12
            Text {
                text: "\u279C Add Waypoint"
                font.pixelSize: 16; font.bold: true; color: "#FFFFFF"
            }
        }
    }

    ColumnLayout {
        spacing: 12

        // Reference point
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Reference:"; font.pixelSize: 13; color: "#F8BBD0" }
            ComboBox {
                id: refCombo
                Layout.fillWidth: true
                model: ["Aircraft Position", "Selected Waypoint"]
                currentIndex: 0
                background: Rectangle { color: "#2D1B4E"; border.color: "#9B59B6"; border.width: 1; radius: 4 }
                contentItem: Text { text: refCombo.displayText; color: "#FFFFFF"; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
            }
            Button {
                text: "\u21C4"
                font.pixelSize: 14
                implicitWidth: 32; implicitHeight: 32
                background: Rectangle { color: "#2D1B4E"; border.color: "#E91E63"; border.width: 1; radius: 4 }
                contentItem: Text { text: "\u21C4"; color: "#E91E63"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: refCombo.currentIndex = refCombo.currentIndex === 0 ? 1 : 0
            }
        }

        // Bearing
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Bearing:"; font.pixelSize: 13; color: "#F8BBD0" }
            SpinBox {
                id: bearingSpin
                from: 0; to: 359; value: 45
                Layout.preferredWidth: 100
                background: Rectangle { color: "#2D1B4E"; border.color: bearingSpin.activeFocus ? "#E91E63" : "#9B59B6"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: bearingSpin.textFromValue(bearingSpin.value, bearingSpin.locale)
                    color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
                Text { text: "\u00B0"; color: "#F8BBD0"; font.pixelSize: 14; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rightMargin: 8 }
            }
        }

        // Distance
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Distance:"; font.pixelSize: 13; color: "#F8BBD0" }
            SpinBox {
                id: distanceSpin
                from: 1; to: 10000; value: 300; stepSize: 10
                Layout.preferredWidth: 120
                background: Rectangle { color: "#2D1B4E"; border.color: distanceSpin.activeFocus ? "#E91E63" : "#9B59B6"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: distanceSpin.textFromValue(distanceSpin.value, distanceSpin.locale)
                    color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
                Text { text: "m"; color: "#F8BBD0"; font.pixelSize: 14; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rightMargin: 8 }
            }
        }

        // Altitude
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Altitude:"; font.pixelSize: 13; color: "#F8BBD0" }
            SpinBox {
                id: altSpin
                from: -100; to: 10000; value: 50
                Layout.preferredWidth: 100
                background: Rectangle { color: "#2D1B4E"; border.color: altSpin.activeFocus ? "#E91E63" : "#9B59B6"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: altSpin.textFromValue(altSpin.value, altSpin.locale)
                    color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
            }
            Switch {
                id: altRelSwitch
                checked: true
                indicator: Rectangle {
                    x: altRelSwitch.leftPadding; y: parent.height / 2 - height / 2
                    width: 40; height: 22; radius: 11
                    color: altRelSwitch.checked ? "#E91E63" : "#2D1B4E"
                    border.color: "#9B59B6"; border.width: 1
                    Rectangle {
                        x: altRelSwitch.checked ? parent.width - width - 2 : 2
                        y: 2; width: 18; height: 18; radius: 9; color: "#FFFFFF"
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }
                }
                Text { text: altRelSwitch.checked ? "Relative" : "Absolute"; color: "#F8BBD0"; font.pixelSize: 12 }
            }
        }

        // Preview section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "#2D1B4E"
            radius: 8
            border.color: "#9B59B6"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 4

                Text { text: "Preview"; font.pixelSize: 12; font.bold: true; color: "#F8BBD0" }

                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "Lat:"; font.pixelSize: 12; color: "#F8BBD0" }
                    Text {
                        text: previewCoord && previewCoord.isValid ? previewCoord.latitude.toFixed(8) : "---"
                        font.pixelSize: 12; color: "#FFFFFF"; font.family: "monospace"
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "Lon:"; font.pixelSize: 12; color: "#F8BBD0" }
                    Text {
                        text: previewCoord && previewCoord.isValid ? previewCoord.longitude.toFixed(8) : "---"
                        font.pixelSize: 12; color: "#FFFFFF"; font.family: "monospace"
                    }
                }

                Text {
                    text: sanityBearingValue >= 0 ? "Bearing back: " + sanityBearingValue.toFixed(2) + "\u00B0 (sanity check)" : ""
                    font.pixelSize: 11; color: "#2ECC71"; font.italic: true
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#1A0A2E"; radius: 4
                    border.color: "#9B59B6"; border.width: 1

                    Item {
                        id: miniMap
                        anchors.fill: parent; anchors.margins: 8

                        Rectangle {
                            x: 10; y: parent.height * 0.3
                            width: 8; height: 8; radius: 4
                            color: "#9B59B6"
                        }
                        Text {
                            x: 22; y: parent.height * 0.3 - 6
                            text: "Ref"; font.pixelSize: 9; color: "#9B59B6"
                        }

                        Rectangle {
                            id: newPointDot
                            x: parent.width * 0.6; y: parent.height * 0.1
                            width: 8; height: 8; radius: 4
                            color: "#E91E63"
                        }
                        Text {
                            x: parent.width * 0.6 + 12; y: parent.height * 0.1 - 6
                            text: "New"; font.pixelSize: 9; color: "#E91E63"
                        }

                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.strokeStyle = "#E91E63"
                                ctx.lineWidth = 1
                                ctx.setLineDash([4, 4])
                                ctx.beginPath()
                                ctx.moveTo(14, parent.height * 0.3 + 4)
                                ctx.lineTo(parent.width * 0.6 + 4, parent.height * 0.1 + 4)
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                Layout.preferredHeight: 36; Layout.preferredWidth: 80
                background: Rectangle { color: "#2D1B4E"; border.color: "#9B59B6"; border.width: 1; radius: 4 }
                contentItem: Text { text: "Cancel"; color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: root.rejected()
            }
            Button {
                id: addBtn
                text: "Add Waypoint"
                highlighted: true
                Layout.preferredHeight: 36; Layout.preferredWidth: 120
                background: Rectangle { color: "#E91E63"; radius: 4 }
                contentItem: Text { text: "Add Waypoint"; color: "#FFFFFF"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: {
                    if (missionStub) {
                        var refCoord = refCombo.currentIndex === 0
                            ? missionStub.aircraftPosition
                            : (missionStub.waypoints.length > 0 ? missionStub.waypoints[missionStub.waypoints.length - 1] : missionStub.aircraftPosition)
                        var newCoord = root.waypointMath.coordinateFromBearingAndDistance(refCoord, bearingSpin.value, distanceSpin.value)
                        missionStub.addWaypoint(newCoord)
                        root.accepted()
                    }
                }
            }
        }
    }
}
