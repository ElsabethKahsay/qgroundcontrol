// Component: CollapsibleSection
// Purpose: Collapsible section UI with a clickable header bar and animated content area.
//   Displays a summary text when collapsed. Uses a default property for child content.
// Properties:
//   headerText (string) — label shown in the header bar
//   summaryText (string) — brief summary shown to the right when collapsed
//   expanded (bool) — whether the content area is visible (default: true)
//   summaryColor (color) — color of the summary text (default: Colors.textPrimary)
// Signals:
//   toggled — emitted when the header bar is clicked
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

ColumnLayout {
    id: root
    property string headerText
    property string summaryText
    property bool expanded: true
    property color summaryColor: Colors.textPrimary
    signal toggled()

    default property alias contentData: contentArea.data

    spacing: root.expanded ? Config.spacingSmall : 0

    Rectangle {
        Layout.fillWidth: true
        height: 28
        radius: Config.radiusSmall
        color: Colors.surfaceLight

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Config.spacingSmall
            anchors.rightMargin: Config.spacingSmall
            spacing: Config.spacingMedium

            Text {
                text: (root.expanded ? "▼" : "▶") + "  " + root.headerText
                font.pixelSize: Config.fontSizeSmall * Config.fontScale
                font.bold: true
                color: Colors.textPrimary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.summaryText
                font.pixelSize: Config.fontSizeSmall * Config.fontScale
                color: root.summaryColor
                font.family: "monospace"
                visible: !root.expanded && root.summaryText.length > 0
                elide: Text.ElideRight
                Layout.maximumWidth: 300
            }
        }
        MouseArea { anchors.fill: parent; onClicked: root.toggled() }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: root.expanded ? Config.spacingSmall : 0
        height: root.expanded ? contentArea.implicitHeight : 0
        clip: true
        color: "transparent"

        Behavior on height {
            NumberAnimation { duration: Config.animNormal; easing.type: Easing.InOutQuad }
        }

        ColumnLayout {
            id: contentArea
            width: parent.width
        }
    }
}
