import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtPositioning
import QtLocation
import com.uav.mission 1.0
import com.uav.preflight 1.0

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

    title: qsTr("Add Waypoint by Bearing & Distance")
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent.width * 0.92)
    closePolicy: Popup.CloseOnEscape
    padding: 16

    background: Rectangle {
        color: "Colors.dialogBg"
        border.color: "Colors.dialogFocus"
        border.width: 2
        radius: 8
    }

    header: Rectangle {
        color: "Colors.dialogSurface"
        radius: 8
        height: 48
        width: parent.width
        RowLayout {
            anchors.fill: parent; anchors.margins: 12
            Text {
                text: qsTr("\u279C Add Waypoint")
                font.pixelSize: 16; font.bold: true; color: "Colors.dialogText"
            }
        }
    }

    ColumnLayout {
        spacing: 12

        // Reference point
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Reference:"; font.pixelSize: 13; color: "Colors.dialogHighlight" }
            ComboBox {
                id: refCombo
                Layout.fillWidth: true
                model: ["Aircraft Position", "Selected Waypoint"]
                currentIndex: 0
                background: Rectangle { color: "Colors.dialogSurface"; border.color: "Colors.dialogAccent"; border.width: 1; radius: 4 }
                contentItem: Text { text: refCombo.displayText; color: "Colors.dialogText"; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
            }
            Button {
                text: qsTr("\u21C4")
                font.pixelSize: 14
                implicitWidth: 32; implicitHeight: 32
                background: Rectangle { color: "Colors.dialogSurface"; border.color: "Colors.dialogFocus"; border.width: 1; radius: 4 }
                contentItem: Text { text: "\u21C4"; color: "Colors.dialogFocus"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: refCombo.currentIndex = refCombo.currentIndex === 0 ? 1 : 0
            }
        }

        // Bearing
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Bearing:"; font.pixelSize: 13; color: "Colors.dialogHighlight" }
            SpinBox {
                id: bearingSpin
                from: 0; to: 359; value: 45
                Layout.preferredWidth: 100
                background: Rectangle { color: "Colors.dialogSurface"; border.color: bearingSpin.activeFocus ? "Colors.dialogFocus" : "Colors.dialogAccent"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: bearingSpin.textFromValue(bearingSpin.value, bearingSpin.locale)
                    color: "Colors.dialogText"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
                Text { text: "\u00B0"; color: "Colors.dialogHighlight"; font.pixelSize: 14; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rightMargin: 8 }
            }
        }

        // Distance
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Distance:"; font.pixelSize: 13; color: "Colors.dialogHighlight" }
            SpinBox {
                id: distanceSpin
                from: 1; to: 10000; value: 300; stepSize: 10
                Layout.preferredWidth: 120
                background: Rectangle { color: "Colors.dialogSurface"; border.color: distanceSpin.activeFocus ? "Colors.dialogFocus" : "Colors.dialogAccent"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: distanceSpin.textFromValue(distanceSpin.value, distanceSpin.locale)
                    color: "Colors.dialogText"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
                Text { text: "m"; color: "Colors.dialogHighlight"; font.pixelSize: 14; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rightMargin: 8 }
            }
        }

        // Altitude
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Altitude:"; font.pixelSize: 13; color: "Colors.dialogHighlight" }
            SpinBox {
                id: altSpin
                from: -100; to: 10000; value: 50
                Layout.preferredWidth: 100
                background: Rectangle { color: "Colors.dialogSurface"; border.color: altSpin.activeFocus ? "Colors.dialogFocus" : "Colors.dialogAccent"; border.width: 1; radius: 4 }
                contentItem: TextInput {
                    text: altSpin.textFromValue(altSpin.value, altSpin.locale)
                    color: "Colors.dialogText"; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                }
            }
            Switch {
                id: altRelSwitch
                checked: true
                indicator: Rectangle {
                    x: altRelSwitch.leftPadding; y: parent.height / 2 - height / 2
                    width: 40; height: 22; radius: 11
                    color: altRelSwitch.checked ? "Colors.dialogFocus" : "Colors.dialogSurface"
                    border.color: "Colors.dialogAccent"; border.width: 1
                    Rectangle {
                        x: altRelSwitch.checked ? parent.width - width - 2 : 2
                        y: 2; width: 18; height: 18; radius: 9; color: "Colors.dialogText"
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }
                }
                Text { text: altRelSwitch.checked ? "Relative" : "Absolute"; color: "Colors.dialogHighlight"; font.pixelSize: 12 }
            }
        }

        // Preview section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "Colors.dialogSurface"
            radius: 8
            border.color: "Colors.dialogAccent"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 4

                Text { text: "Preview"; font.pixelSize: 12; font.bold: true; color: "Colors.dialogHighlight" }

                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "Lat:"; font.pixelSize: 12; color: "Colors.dialogHighlight" }
                    Text {
                        text: previewCoord && previewCoord.isValid ? previewCoord.latitude.toFixed(8) : "---"
                        font.pixelSize: 12; color: "Colors.dialogText"; font.family: "monospace"
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "Lon:"; font.pixelSize: 12; color: "Colors.dialogHighlight" }
                    Text {
                        text: previewCoord && previewCoord.isValid ? previewCoord.longitude.toFixed(8) : "---"
                        font.pixelSize: 12; color: "Colors.dialogText"; font.family: "monospace"
                    }
                }

                Text {
                    text: sanityBearingValue >= 0 ? "Bearing back: " + sanityBearingValue.toFixed(2) + "\u00B0 (sanity check)" : ""
                    font.pixelSize: 11; color: "Colors.success"; font.italic: true
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "Colors.dialogBg"; radius: 4
                    border.color: "Colors.dialogAccent"; border.width: 1

                    Item {
                        id: miniMap
                        anchors.fill: parent; anchors.margins: 8

                        Rectangle {
                            x: 10; y: parent.height * 0.3
                            width: 8; height: 8; radius: 4
                            color: "Colors.dialogAccent"
                        }
                        Text {
                            x: 22; y: parent.height * 0.3 - 6
                            text: "Ref"; font.pixelSize: 9; color: "Colors.dialogAccent"
                        }

                        Rectangle {
                            id: newPointDot
                            x: parent.width * 0.6; y: parent.height * 0.1
                            width: 8; height: 8; radius: 4
                            color: "Colors.dialogFocus"
                        }
                        Text {
                            x: parent.width * 0.6 + 12; y: parent.height * 0.1 - 6
                            text: "New"; font.pixelSize: 9; color: "Colors.dialogFocus"
                        }

                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.strokeStyle = "Colors.dialogFocus"
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
                text: qsTr("Cancel")
                Layout.preferredHeight: 36; Layout.preferredWidth: 80
                background: Rectangle { color: "Colors.dialogSurface"; border.color: "Colors.dialogAccent"; border.width: 1; radius: 4 }
                contentItem: Text { text: "Cancel"; color: "Colors.dialogText"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: root.rejected()
            }
            Button {
                id: addBtn
                text: qsTr("Add Waypoint")
                highlighted: true
                Layout.preferredHeight: 36; Layout.preferredWidth: 120
                background: Rectangle { color: "Colors.dialogFocus"; radius: 4 }
                contentItem: Text { text: "Add Waypoint"; color: "Colors.dialogText"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
