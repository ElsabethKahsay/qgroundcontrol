import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root
    width: parent ? parent.width : 200
    height: colLayout.height + Config.spacingMedium * 2
    radius: Config.radiusMedium
    color: Colors.surface
    border.color: status === "passed" ? Colors.success
                 : status === "failed" ? Colors.error
                 : isRunning ? Colors.warning
                 : Colors.border
    border.width: status === "passed" || status === "failed" || isRunning ? 2 : 1

    property string title: "Control Surface Sweep"
    property string status: "pending"
    property string evaluationMessage: ""
    property string progressText: ""
    property bool isRunning: false
    property bool reArmFailed: false

    signal runRequested()

    function channelValue(index) {
        var raw = TelemetryProvider.servoOutputsString
        if (!raw || raw.length === 0) return "\u2014"
        var parts = raw.trim().split(/\s+/)
        if (parts.length > index) return parts[index]
        return "\u2014"
    }

    function channelLabel(index) {
        var labels = ["", "Aileron", "Elevator", "Throttle", "Rudder",
                      "Ch5", "Ch6", "Tilt L", "Tilt R"]
        return index < labels.length ? labels[index] : "Ch" + index
    }

    ColumnLayout {
        id: colLayout
        anchors {
            left: parent.left; right: parent.right; top: parent.top
            margins: Config.spacingMedium
        }
        spacing: Config.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                radius: 7
                color: status === "passed" ? Colors.success
                     : status === "failed" ? Colors.error
                     : isRunning ? Colors.warning
                     : Colors.textSecondary
            }

            Label {
                text: title
                color: Colors.textPrimary
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: {
                    if (isRunning) return "\u23F3 Sweeping..."
                    if (status === "passed") return "Passed"
                    return "Run Servo Sweep"
                }
                enabled: !isRunning && status !== "passed"
                highlighted: !isRunning && status === "pending"
                font.pixelSize: status === "passed" ? 13 : 14
                font.bold: true
                implicitHeight: 36
                implicitWidth: status === "passed" ? 80 : 150

                onClicked: root.runRequested()

                background: Rectangle {
                    radius: Config.radiusSmall
                    color: {
                        if (status === "passed") return Colors.successDim
                        if (isRunning) return Colors.surfaceLight
                        return Colors.warning
                    }
                    border.color: {
                        if (status === "passed") return Colors.success
                        return "transparent"
                    }
                    border.width: 1
                }

                contentItem: Text {
                    text: parent.text
                    color: status === "passed" ? Colors.success : Colors.background
                    font.pixelSize: parent.font.pixelSize
                    font.bold: parent.font.bold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Disarm warning banner
        Rectangle {
            Layout.fillWidth: true
            visible: TelemetryProvider.armed && !isRunning
            height: 32
            radius: Config.radiusSmall
            color: Colors.warning

            Label {
                anchors.centerIn: parent
                text: "Vehicle is ARMED \u2014 disarm before sweep"
                color: Colors.background
                font.pixelSize: 13
                font.bold: true
            }
        }

        // Re-arm failed banner
        Rectangle {
            Layout.fillWidth: true
            visible: reArmFailed
            height: 32
            radius: Config.radiusSmall
            color: Colors.error

            Label {
                anchors.centerIn: parent
                text: "Re-arm failed \u2014 arm manually"
                color: Colors.background
                font.pixelSize: 13
                font.bold: true
            }
        }

        Label {
            visible: progressText.length > 0
            text: progressText
            color: Colors.warning
            font.pixelSize: 13
            font.italic: true
        }

        // Step progress bar
        ProgressBar {
            Layout.fillWidth: true
            visible: isRunning
            from: 0.0
            to: 1.0
            value: HardwareTestController.stepProgress

            background: Rectangle {
                implicitHeight: 10
                radius: 5
                color: Colors.surfaceLight
            }
            contentItem: Rectangle {
                implicitHeight: 10
                radius: 5
                color: Colors.warning
            }
        }

        // Channel list during sweep
        Rectangle {
            Layout.fillWidth: true
            visible: isRunning
            height: labelCol.height + Config.spacingSmall * 2
            radius: Config.radiusSmall
            color: Colors.surfaceLight

            GridLayout {
                id: labelCol
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                columns: 3
                columnSpacing: Config.spacingMedium
                rowSpacing: 2

                Repeater {
                    model: [
                        { ch: 1, label: "Ch1" },
                        { ch: 2, label: "Ch2" },
                        { ch: 3, label: "Ch3" },
                        { ch: 4, label: "Ch4" },
                        { ch: 5, label: "Ch5" },
                        { ch: 6, label: "Ch6" },
                        { ch: 7, label: "Ch7" },
                        { ch: 8, label: "Ch8" }
                    ]

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall

                        Label {
                            text: modelData.label
                            font.pixelSize: 13
                            font.family: "monospace"
                            font.bold: true
                            color: Colors.textSecondary
                            Layout.preferredWidth: 40
                        }
                        Label {
                            text: root.channelLabel(modelData.ch)
                            font.pixelSize: 13
                            color: Colors.textSecondary
                            Layout.preferredWidth: 80
                            elide: Text.ElideRight
                        }
                        Label {
                            text: root.channelValue(modelData.ch - 1)
                            font.pixelSize: 13
                            font.family: "monospace"
                            color: Colors.primary
                            Layout.preferredWidth: 56
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }

        // Result message
        Rectangle {
            Layout.fillWidth: true
            visible: evaluationMessage.length > 0 && !isRunning
            height: 32
            radius: Config.radiusSmall
            color: status === "failed" ? Qt.rgba(1.0, 0.1, 0.1, 0.1)
                 : Qt.rgba(0.0, 0.9, 0.4, 0.1)

            Label {
                anchors.centerIn: parent
                text: evaluationMessage
                color: status === "failed" ? Colors.error : Colors.success
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.WordWrap
            }
        }
    }
}
