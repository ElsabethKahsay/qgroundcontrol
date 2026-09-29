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
            // Route every arm request through the single gate-checked path
            // (TelemetryBridge::arm). The gate decision is centralized there,
            // not duplicated per widget.
            if (typeof TelemetryProvider !== 'undefined' && TelemetryProvider !== null) {
                TelemetryProvider.arm()
            } else {
                // No TelemetryProvider available — cannot arm through gate.
                // Do NOT fall back to mainWindow.armVehicleRequest() as that
                // bypasses the ArmingGate entirely. The gate is the single
                // source of truth; if the provider is unavailable, arm is denied.
                qCWarning("armedindicator") << "Arm requested but TelemetryProvider unavailable — arm denied by gate"
            }
        } else {
            mainWindow.disarmVehicleRequest()
        }
        currentIndex = -1
    }
}
