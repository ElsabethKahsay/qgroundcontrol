// AirButton.qml
// Reusable themed push button for the Airspace page.
//
// Lives in pages/ so AirspacePage.qml can resolve it as a sibling type
// (QML resolves components in the same directory without an import).
// Deliberately does NOT depend on the unregistered CustomButton in cpts/.

import QtQuick
import com.uav.preflight 1.0

Rectangle {
    id: control

    property string text: ""
    property color baseColor: Colors.surface
    property color textColor: Colors.textPrimary
    property bool bold: false
    property bool btnEnabled: true

    signal clicked

    radius: Config.radiusSmall
    border.color: Colors.border
    border.width: 1
    color: btnEnabled ? baseColor : Colors.surfaceLight
    opacity: btnEnabled ? 1.0 : 0.6
    implicitHeight: 30
    implicitWidth: Math.max(88, label.implicitWidth + Config.spacingMedium * 2)

    Text {
        id: label
        anchors.centerIn: parent
        text: control.text
        font.pixelSize: Config.fontSizeSmall
        font.bold: control.bold
        color: btnEnabled ? control.textColor : Colors.textDisabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.btnEnabled
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
