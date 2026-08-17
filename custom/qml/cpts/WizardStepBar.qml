// Component: WizardStepBar
// Purpose: Top step indicator for the multi-step preflight wizard.
//   Shows steps with check marks for completed, highlight for active, dimmed for future.
// Properties:
//   currentStep (int) — zero-based index of the active step (0=Operator, 1=System, 2=Hardware, 3=Review)
import QtQuick
import QtQuick.Layouts
import com.uav.preflight 1.0

Item {
    id: root

    property int currentStep: 0

    readonly property var steps: [
        { label: qsTr("Operator"),  icon: "\uD83D\uDC64" },
        { label: qsTr("System"),    icon: "\uD83D\uDD27" },
        { label: qsTr("Hardware"),  icon: "\uD83D\uDCF7" },
        { label: qsTr("Review"),    icon: "\u2713"       }
    ]

    implicitHeight: 48

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.steps

            delegate: Item {
                id: stepItem
                required property var modelData
                required property int index

                Layout.fillWidth: true
                Layout.fillHeight: true

                readonly property bool isCompleted: index < root.currentStep
                readonly property bool isActive:    index === root.currentStep
                readonly property bool isFuture:    index > root.currentStep

                // Connector line left
                Rectangle {
                    visible: index > 0
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.5 - 18
                    height: 2
                    color: isCompleted || isActive ? Colors.accent : Colors.borderLight
                    Behavior on color { ColorAnimation { duration: Config.animNormal } }
                }

                // Connector line right
                Rectangle {
                    visible: index < root.steps.length - 1
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.5 - 18
                    height: 2
                    color: isCompleted ? Colors.accent : Colors.borderLight
                    Behavior on color { ColorAnimation { duration: Config.animNormal } }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 3

                    // Circle badge
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 32; height: 32; radius: 16
                        color: isCompleted ? Colors.accent
                             : isActive    ? Colors.accentDim
                             : "transparent"
                        border.color: isCompleted ? Colors.accent
                                    : isActive    ? Colors.accent
                                    : Colors.borderLight
                        border.width: isActive ? 2 : 1

                        Behavior on color       { ColorAnimation { duration: Config.animNormal } }
                        Behavior on border.color { ColorAnimation { duration: Config.animNormal } }

                        Text {
                            anchors.centerIn: parent
                            text: stepItem.isCompleted ? "\u2713" : modelData.icon
                            font.pixelSize: 13
                            font.bold: stepItem.isCompleted || stepItem.isActive
                            color: stepItem.isCompleted ? Colors.background
                                 : stepItem.isActive    ? Colors.accent
                                 : Colors.textDisabled
                        }
                    }

                    // Label
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: Config.fontSizeSmall
                        font.bold: isActive
                        color: isCompleted ? Colors.textSecondary
                             : isActive    ? Colors.textPrimary
                             : Colors.textDisabled
                        Behavior on color { ColorAnimation { duration: Config.animNormal } }
                    }
                }
            }
        }
    }
}
