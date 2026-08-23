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

    Layout.fillWidth: true
    spacing: root.expanded ? Config.spacingSmall : 0

    Rectangle {
        id: headerRect
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        implicitHeight: 36
        radius: Config.radiusSmall
        color: Colors.surfaceLight

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Config.spacingMedium
            anchors.rightMargin: Config.spacingMedium
            spacing: Config.spacingMedium

            Text {
                text: (root.expanded ? "▼" : "▶") + "  " + root.headerText
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textPrimary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.summaryText
                font.pixelSize: Config.fontSizeBody
                color: root.summaryColor
                font.family: "monospace"
                visible: !root.expanded && root.summaryText.length > 0
                elide: Text.ElideRight
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
            }
        }
        MouseArea { anchors.fill: parent; onClicked: root.toggled() }
    }

    Rectangle {
        id: containerRect
        Layout.fillWidth: true
        Layout.topMargin: root.expanded ? Config.spacingSmall : 0
        Layout.preferredHeight: height
        implicitHeight: height
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

