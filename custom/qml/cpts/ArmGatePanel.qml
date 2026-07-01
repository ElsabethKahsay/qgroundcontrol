import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root
    color: Colors.footerBg
    border.color: Colors.border
    border.width: 1

    readonly property bool gateOpen: {
        if (ArmingGate.overrideActive) return true
        return PreflightManager.allMandatoryPassed
    }

    readonly property int blockerCount: PreflightChecklistModel.blockingFailedCount

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Config.spacingMedium
        anchors.rightMargin: Config.spacingMedium
        spacing: Config.spacingMedium

        // Gate icon + status
        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 14
            color: gateOpen ? Colors.successDim : Colors.errorDim
            border.color: gateOpen ? Colors.success : Colors.error
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: gateOpen ? "\u2713" : "\u2717"
                font.pixelSize: 14
                font.bold: true
                color: gateOpen ? Colors.success : Colors.error
            }
        }

        ColumnLayout {
            spacing: 0
            Text {
                text: "Arming Gate"
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
                color: Colors.textPrimary
            }
            Text {
                text: gateOpen ? "Open \u2014 arm allowed"
                      : "Closed \u2014 " + blockerCount + " blocker(s)"
                font.pixelSize: Config.fontSizeSmall
                color: gateOpen ? Colors.success : Colors.error
            }
        }

        Item { Layout.fillWidth: true }

        // Gate mode pill
        Rectangle {
            Layout.preferredWidth: 88
            Layout.preferredHeight: 32
            radius: 16
            color: ArmingGate.mode === 0 ? Colors.warningDim
                 : ArmingGate.mode === 1 ? Colors.success
                 : Colors.info
            border.color: ArmingGate.mode === 1 ? Colors.success : Colors.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: ArmingGate.mode === 0 ? "PASSIVE"
                     : ArmingGate.mode === 1 ? "ACTIVE"
                     : "HYBRID"
                font.pixelSize: 11
                font.bold: true
                color: ArmingGate.mode === 1 ? Colors.background : Colors.textPrimary
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ArmingGate.mode = (ArmingGate.mode + 1) % 3
                cursorShape: Qt.PointingHandCursor
            }
        }

        // Override button
        Rectangle {
            Layout.preferredHeight: 32
            Layout.preferredWidth: 96
            radius: 16
            color: Colors.error
            visible: !gateOpen
            Text {
                anchors.centerIn: parent
                text: "Override"
                font.pixelSize: 11
                font.bold: true
                color: Colors.background
            }
            MouseArea {
                anchors.fill: parent
                onClicked: ArmingGate.overrideGate("Override from Arming Gate panel")
                cursorShape: Qt.PointingHandCursor
            }
        }

        // Force Arm button
        Rectangle {
            Layout.preferredHeight: 32
            Layout.preferredWidth: 104
            radius: 16
            color: Colors.warning
            Text {
                anchors.centerIn: parent
                text: "Force Arm"
                font.pixelSize: 11
                font.bold: true
                color: Colors.background
            }
            MouseArea {
                anchors.fill: parent
                onClicked: ArmingGate.forceArm()
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
