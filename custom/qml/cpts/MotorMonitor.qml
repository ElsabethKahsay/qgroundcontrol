import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Item {
    id: root
    implicitHeight: col.implicitHeight + 16

    property int barHeight: 20
    property int barRadius: 3
    property color barIdle: Colors.disabled
    property color barNormal: Colors.success
    property color barWarning: "#e6a817"
    property color barError: Colors.danger

    Column {
        id: col
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text {
            text: qsTr("Motor Outputs")
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }

        Text {
            text: TelemetryProvider
                ? (TelemetryProvider.motorCount + " motors  ·  "
                    + (TelemetryProvider.isConnected ? "Live" : "Disconnected"))
                : "No telemetry"
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
        }

        Repeater {
            id: barRepeater
            model: TelemetryProvider ? TelemetryProvider.motorCount : 0

            delegate: Item {
                width: parent.width
                height: root.barHeight + 18

                property int pwmValue: TelemetryProvider
                    ? TelemetryProvider.motorOutputs[index] : 0

                Text {
                    id: chanLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: pwmBar.verticalCenter
                    width: 40
                    text: "Ch" + (index + 1)
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: Colors.textPrimary
                }

                Rectangle {
                    id: pwmBar
                    anchors.left: chanLabel.right
                    anchors.leftMargin: 4
                    anchors.right: numericLabel.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    height: root.barHeight
                    radius: root.barRadius

                    color: {
                        var v = pwmValue
                        if (v === 0) return root.barIdle
                        if (v < Config.kPwmRangeMin || v > Config.kPwmRangeMax) return root.barError
                        return root.barNormal
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(pwmValue / Config.kPwmMax, 1.0)
                        radius: root.barRadius
                        color: Qt.lighter(parent.color, 1.2)
                    }

                    Text {
                        anchors.centerIn: parent
                        text: pwmValue.toString()
                        font.pixelSize: Config.fontSizeSmall
                        font.family: "monospace"
                        color: pwmValue === 0 ? Colors.textSecondary : Colors.textPrimary
                        visible: pwmValue > 0
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.text: "Channel " + (index + 1) + ": " + pwmValue + " µs"
                        ToolTip.delay: 400
                    }
                }

                Text {
                    id: numericLabel
                    anchors.right: parent.right
                    anchors.verticalCenter: pwmBar.verticalCenter
                    width: 60
                    horizontalAlignment: Text.AlignRight
                    text: {
                        var v = pwmValue
                        if (v === 0) return "—"
                        var pct = Math.round((v - Config.kPwmMin) / (Config.kPwmMax - Config.kPwmMin) * 100)
                        return pct + "%"
                    }
                    font.pixelSize: Config.fontSizeSmall
                    font.family: "monospace"
                    color: Colors.textSecondary
                }
            }
        }

        Text {
            text: qsTr("No motor data — connect to vehicle")
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            visible: barRepeater.count === 0
        }
    }
}
