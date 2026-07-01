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
                 : isRunning ? Colors.primary
                 : Colors.border
    border.width: status === "passed" || status === "failed" || isRunning ? 2 : 1

    property string title: "Motor Test"
    property string status: "pending"
    property string evaluationMessage: ""
    property string progressText: ""
    property bool isRunning: false

    signal runRequested()

    function channelValue(index) {
        var raw = TelemetryProvider.servoOutputsString
        if (!raw || raw.length === 0) return "\u2014"
        var parts = raw.trim().split(/\s+/)
        if (parts.length > index) return parts[index]
        return "\u2014"
    }

    ColumnLayout {
        id: colLayout
        anchors {
            left: parent.left; right: parent.right; top: parent.top
            margins: Config.spacingMedium
        }
        spacing: Config.spacingSmall

        // Header row: status dot + title + action button
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                radius: 7
                color: status === "passed" ? Colors.success
                     : status === "failed" ? Colors.error
                     : isRunning ? Colors.primary
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
                id: runBtn
                text: {
                    if (isRunning) return "\u23F3 Running..."
                    if (status === "passed") return "Passed"
                    return "Run Motor Test"
                }
                enabled: !isRunning && status !== "passed"
                highlighted: !isRunning && status === "pending"
                font.pixelSize: status === "passed" ? 13 : 14
                font.bold: true
                implicitHeight: 36
                implicitWidth: status === "passed" ? 80 : 140

                onClicked: root.runRequested()

                background: Rectangle {
                    radius: Config.radiusSmall
                    color: {
                        if (status === "passed") return Colors.successDim
                        if (isRunning) return Colors.surfaceLight
                        return Colors.primary
                    }
                    border.color: {
                        if (status === "passed") return Colors.success
                        if (isRunning) return "transparent"
                        return Colors.primary
                    }
                    border.width: 1

                    // Pulse animation when pending
                    SequentialAnimation on color {
                        running: !isRunning && status === "pending" && runBtn.hovered
                        loops: Animation.Infinite
                        ColorAnimation { from: Colors.primary; to: Colors.accent; duration: 600 }
                        ColorAnimation { from: Colors.accent; to: Colors.primary; duration: 600 }
                    }
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

        // Step progress text
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
                color: Colors.primary
            }
        }

        // Live servo output preview during test
        Rectangle {
            Layout.fillWidth: true
            visible: isRunning
            height: 80
            radius: Config.radiusSmall
            color: Colors.surfaceLight

            GridLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                columns: 5
                columnSpacing: Config.spacingSmall
                rowSpacing: 2

                Label { text: "" }

                Repeater {
                    model: 4
                    Label {
                        text: "Ch" + (modelData + 1)
                        font.pixelSize: 14
                        font.family: "monospace"
                        font.bold: true
                        color: Colors.textPrimary
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Label {
                    text: "PWM"
                    font.pixelSize: 12
                    font.bold: true
                    color: Colors.textSecondary
                }
                Repeater {
                    model: 4
                    Label {
                        text: root.channelValue(modelData)
                        font.pixelSize: 13
                        font.family: "monospace"
                        color: Colors.primary
                        font.bold: true
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignHCenter
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
                font.family: evaluationMessage.indexOf("\u00b5s") >= 0 ? "monospace" : "sans-serif"
            }
        }
    }
}
