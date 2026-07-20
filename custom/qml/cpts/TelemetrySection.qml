import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

ColumnLayout {
    id: root

    property string headerText
    property string summaryText
    property bool expanded: false
    property color summaryColor: Colors.textSecondary
    property color headerColor: "#E0E0E0"

    signal toggled()

    default property alias contentData: contentArea.data

    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.minimumHeight: Config.kCollapsedHeight
        height: Config.kCollapsedHeight
        radius: Config.radiusSmall
        color: Colors.surfaceLight

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Config.spacingMedium
            anchors.rightMargin: Config.spacingMedium
            spacing: Config.spacingSmall

            Text {
                text: root.expanded ? "\u25BC" : "\u25B6"
                font.pixelSize: 12
                color: Colors.textSecondary
                Layout.preferredWidth: 16
            }

            Text {
                text: root.headerText
                font.pixelSize: 16
                font.bold: true
                color: root.headerColor
                Layout.fillWidth: true
            }

            Text {
                text: root.summaryText
                font.pixelSize: 12
                font.family: "monospace"
                color: root.summaryColor
                visible: !root.expanded && root.summaryText.length > 0
                elide: Text.ElideRight
                Layout.maximumWidth: 200
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.toggled()
            cursorShape: Qt.PointingHandCursor
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: root.expanded ? Config.spacingSmall : 0
        height: root.expanded ? contentArea.implicitHeight + Config.spacingSmall : 0
        clip: true
        color: "transparent"

        Behavior on height {
            NumberAnimation {
                duration: Config.animNormal
                easing.type: Easing.InOutQuad
            }
        }

        ColumnLayout {
            id: contentArea
            width: parent.width
            spacing: Config.spacingSmall

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Config.spacingMedium
                Layout.rightMargin: Config.spacingMedium
                height: 1
                color: Colors.border
                visible: root.expanded
            }
        }
    }
}
