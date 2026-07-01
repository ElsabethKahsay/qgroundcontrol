import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root

    property int propellerCount: 4
    property var propellerLabels: ["Prop 1 (CW)", "Prop 2 (CCW)", "Prop 3 (CW)", "Prop 4 (CCW)"]
    readonly property int confirmedCount: {
        var n = 0
        for (var i = 0; i < propRepeater.count; ++i) {
            var item = propRepeater.itemAt(i)
            if (item && item.checked) n++
        }
        return n
    }
    readonly property bool allConfirmed: confirmedCount >= propellerCount

    signal allConfirmedChanged()

    color: Colors.surface
    radius: Config.radiusMedium
    border.color: Colors.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: Config.spacingMedium
        anchors.margins: Config.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: "\u2699 Propeller Inspection"
                font.pixelSize: Config.fontSizeH2
                font.bold: true
                color: Colors.accent
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 14
                color: root.allConfirmed ? Colors.successDim : Colors.checkWarnDim
                border.color: root.allConfirmed ? Colors.success : Colors.checkWarn
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.allConfirmed ? "\u2713" : String(root.confirmedCount)
                    font.pixelSize: 12
                    font.bold: true
                    color: root.allConfirmed ? Colors.success : Colors.checkWarn
                }
            }
        }

        Text {
            text: "Visually inspect each propeller for cracks, chips, and secure mounting"
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            rowSpacing: Config.spacingSmall
            columnSpacing: Config.spacingSmall
            Layout.fillWidth: true

            Repeater {
                id: propRepeater
                model: root.propellerCount

                Rectangle {
                    required property int index
                    property bool checked: false

                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: checked ? Colors.successDim : Colors.surfaceLight
                    radius: Config.radiusSmall
                    border.color: checked ? Colors.success : Colors.border
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Config.animFast } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingMedium
                        spacing: Config.spacingSmall

                        Text {
                            text: checked ? "\u2713" : "\u25CB"
                            font.pixelSize: 16
                            color: checked ? Colors.success : Colors.textDisabled
                            font.bold: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: root.propellerLabels[index]
                                font.pixelSize: Config.fontSizeBody
                                font.bold: true
                                color: Colors.textPrimary
                            }
                            Text {
                                text: checked ? "Inspected OK" : "Tap to confirm"
                                font.pixelSize: Config.fontSizeSmall
                                color: checked ? Colors.success : Colors.textSecondary
                            }
                        }

                        Switch {
                            checked: parent.checked
                            onCheckedChanged: {
                                parent.checked = checked
                                if (root.allConfirmed)
                                    root.allConfirmedChanged()
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: parent.checked = !parent.checked
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1 }

        Button {
            text: root.allConfirmed ? "\u2713 All Propellers Confirmed" : "Confirm All (" + confirmedCount + "/" + propellerCount + ")"
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            highlighted: root.allConfirmed
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            enabled: root.allConfirmed || confirmedCount > 0
            palette.highlight: root.allConfirmed ? Colors.success : Colors.accent

            onClicked: {
                for (var i = 0; i < propRepeater.count; ++i) {
                    var item = propRepeater.itemAt(i)
                    if (item) item.checked = true
                }
            }
        }

        Text {
            text: root.allConfirmed ? "All propellers inspected and confirmed"
                                    : confirmedCount + " of " + propellerCount + " propellers confirmed"
            font.pixelSize: Config.fontSizeSmall
            color: root.allConfirmed ? Colors.success : Colors.textSecondary
            font.bold: root.allConfirmed
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
