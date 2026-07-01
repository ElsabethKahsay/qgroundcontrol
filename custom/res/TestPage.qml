import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    Rectangle {
        anchors.fill: parent
        color: "#1A0A2E"

        Rectangle {
            anchors.centerIn: parent
            width: 200
            height: 200
            color: "#E91E63"
            radius: 10

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 16

                Text {
                    text: "Preflight Plugin Active"
                    font.pixelSize: 24
                    font.bold: true
                    color: "#FFFFFF"
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "Pink rectangle on dark purple background"
                    font.pixelSize: 14
                    color: "#FFFFFF"
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
