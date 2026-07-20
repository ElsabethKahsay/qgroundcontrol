import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Rectangle {
    id: root
    width: parent ? parent.width : 200
    height: 64
    color: Colors.surface
    radius: Config.radiusMedium
    
    property string title: qsTr("Hardware Test")
    property string status: "pending"
    
    Row {
        anchors.fill: parent
        anchors.margins: Config.spacingMedium
        spacing: Config.spacingMedium
        
        Rectangle {
            width: 12; height: 12; radius: 6
            color: status === "passed" ? Colors.success : 
                   status === "failed" ? Colors.error : Colors.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        
        Label {
            text: title
            color: Colors.textPrimary
            font.pixelSize: Config.fontSizeBody
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
