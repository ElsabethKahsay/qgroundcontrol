import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root

    property int motorCount: 4
    property var motorLabels: ["M1", "M2", "M3", "M4"]
    property var spinDirections: ["CW", "CCW", "CW", "CCW"]

    color: Colors.surface
    radius: Config.radiusMedium
    border.color: Colors.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: Config.spacingMedium
        anchors.margins: Config.spacingMedium

        Text {
            text: "Motor Test Panel"
            font.pixelSize: Config.fontSizeH2
            font.bold: true
            color: Colors.accent
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Colors.checkWarnDim
            radius: Config.radiusSmall
            visible: HardwareTestController.isRunning
            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                Text {
                    text: "\u26A0 Motor test in progress — ensure vehicle is disarmed and props removed"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.checkWarn
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            text: "HardwareTestController " + (HardwareTestController.isRunning ? "RUNNING" : "IDLE")
            font.pixelSize: Config.fontSizeSmall
            color: HardwareTestController.isRunning ? Colors.warning : Colors.textSecondary
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            clip: true
            contentHeight: motorGrid.implicitHeight

            GridLayout {
                id: motorGrid
                columns: 2
                rowSpacing: Config.spacingSmall
                columnSpacing: Config.spacingSmall
                width: parent.width

                Repeater {
                    model: root.motorCount

                    Rectangle {
                        required property int index

                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: Colors.border
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Config.spacingSmall
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Config.spacingSmall
                                Text {
                                    text: root.motorLabels[index]
                                    font.pixelSize: Config.fontSizeBody
                                    font.bold: true
                                    color: Colors.textPrimary
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: root.spinDirections[index]
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.info
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Config.spacingSmall
                                Text {
                                    text: "Throttle:"
                                    font.pixelSize: Config.fontSizeSmall
                                    color: Colors.textSecondary
                                }
                                Slider {
                                    id: throttleSlider
                                    Layout.fillWidth: true
                                    from: 0; to: 100; value: 30
                                    stepSize: 1
                                    enabled: !HardwareTestController.isRunning
                                    background: Rectangle {
                                        x: throttleSlider.leftPadding
                                        y: throttleSlider.topPadding + throttleSlider.availableHeight / 2 - height / 2
                                        width: throttleSlider.availableWidth
                                        height: 4
                                        radius: 2
                                        color: Colors.borderLight
                                        Rectangle {
                                            width: throttleSlider.visualPosition * parent.width
                                            height: parent.height
                                            color: Colors.accent
                                            radius: 2
                                        }
                                    }
                                    handle: Rectangle {
                                        x: throttleSlider.leftPadding + throttleSlider.visualPosition * (throttleSlider.availableWidth - width)
                                        y: throttleSlider.topPadding + throttleSlider.availableHeight / 2 - height / 2
                                        width: 18
                                        height: 18
                                        radius: 9
                                        color: throttleSlider.pressed ? Colors.accent : Colors.textPrimary
                                        border.color: Colors.border
                                        border.width: 1
                                    }
                                }
                                Text {
                                    text: Math.round(throttleSlider.value) + "%"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textPrimary
                                    Layout.preferredWidth: 36
                                }
                            }

                            Text {
                                text: "Duration: " + durationSpin.value + "s"
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textSecondary
                            }

                            SpinBox {
                                id: durationSpin
                                from: 1; to: 30; value: 3
                                enabled: !HardwareTestController.isRunning
                                Layout.preferredHeight: 24
                                Layout.preferredWidth: 80
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Colors.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Button {
                text: HardwareTestController.isRunning ? "Abort" : "Start All Motors"
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                highlighted: !HardwareTestController.isRunning
                Layout.preferredHeight: 40
                Layout.fillWidth: true
                palette.highlight: HardwareTestController.isRunning ? Colors.error : Colors.accent

                onClicked: {
                    if (HardwareTestController.isRunning) {
                        HardwareTestController.abortSequence()
                    } else {
                        HardwareTestController.runMotorTest()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Button {
                text: "Spin Single"
                font.pixelSize: Config.fontSizeSmall
                Layout.preferredHeight: 32
                Layout.fillWidth: true
                enabled: !HardwareTestController.isRunning
                onClicked: HardwareTestController.runMotorTest()
            }

            Button {
                text: "Servo Sweep"
                font.pixelSize: Config.fontSizeSmall
                Layout.preferredHeight: 32
                Layout.fillWidth: true
                enabled: !HardwareTestController.isRunning
                onClicked: HardwareTestController.runServoSweep()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            color: HardwareTestController.allPassed ? Colors.successDim : Colors.errorDim
            radius: Config.radiusSmall
            visible: HardwareTestController.sequenceCompleted !== undefined

            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                Text {
                    text: HardwareTestController.allPassed ? "\u2713 All motors passed" : "\u2717 Test failed"
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: HardwareTestController.allPassed ? Colors.success : Colors.error
                }
                Text {
                    text: HardwareTestController.lastErrorMessage
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }
    }
}
