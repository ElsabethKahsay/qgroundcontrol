import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.Palette

QGCComboBox {
    anchors.verticalCenter: parent.verticalCenter
    alternateText:          _armed ? qsTr("Armed") : qsTr("Disarmed")
    model:                  [ qsTr("Arm"), qsTr("Disarm") ]
    font.pointSize:         ScreenTools.mediumFontPointSize
    currentIndex:           -1
    sizeToContents:         true

    property bool showIndicator: true

    property var    _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool   _armed:         _activeVehicle ? _activeVehicle.armed : false

    onActivated: {
        if (index == 0) {
            if (typeof ArmingGate !== 'undefined' && ArmingGate !== null) {
                var criticalFails = typeof PreflightManager !== 'undefined' && PreflightManager !== null
                    ? PreflightManager.failedChecks : 0
                var result = ArmingGate.processArmRequest(criticalFails, 0)
                if (result === ArmingGate.GATE_CLOSED) {
                    currentIndex = -1
                    return
                }
            }
            mainWindow.armVehicleRequest()
        } else {
            mainWindow.disarmVehicleRequest()
        }
        currentIndex = -1
    }
}
