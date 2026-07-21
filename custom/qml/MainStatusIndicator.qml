/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

// Component: MainStatusIndicator
// Purpose: Main status indicator in the top toolbar showing vehicle connection state, armed/flying
//   status, estimated flight range (Phase 7), and a sensor status popup. Integrates with the
//   ArmingGate and PreflightManager for arm-blocking override dialogs.
// Properties:
//   _gateCriticalFails (int) — number of critical failures passed to ArmGateDialog
//   _gateManualFails (int) — number of manual pending checks passed to ArmGateDialog
//   _estRangeText (string) — estimated flight range text from PowerModel
//   _estLabelColor (string) — color of the estimated range label
import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.FactSystem
import com.uav.preflight 1.0
import cpts 1.0 as ADL

RowLayout {
    id:         _root
    spacing:    0

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property var    _vehicleInAir:      _activeVehicle ? _activeVehicle.flying || _activeVehicle.landing : false
    property bool   _vtolInFWDFlight:   _activeVehicle ? _activeVehicle.vtolInFwdFlight : false
    property bool   _armed:             _activeVehicle ? _activeVehicle.armed : false
    property real   _margins:           ScreenTools.defaultFontPixelWidth
    property real   _spacing:           ScreenTools.defaultFontPixelWidth / 2
    property bool   _healthAndArmingChecksSupported: _activeVehicle ? _activeVehicle.healthAndArmingCheckReport.supported : false

    property int   _gateCriticalFails:  0
    property int   _gateManualFails:    0

    // Phase 7: Estimated flight time/range from power model
    property string _estRangeText: ""
    property string _estLabelColor: Colors.textDisabled

    ADL.ArmGateDialogLoader {
        id: armGateDialogLoader
        criticalFailCount: _root._gateCriticalFails
        manualFailCount: _root._gateManualFails
        onAccepted: {
            mainWindow.armVehicleRequest()
            mainWindow.hideIndicatorPopup()
        }
    }

    ColumnLayout {
        spacing: 0

        QGCLabel {
            id:             mainStatusLabel
            text:           mainStatusText()
            font.pointSize: _vehicleInAir ? ScreenTools.defaultFontPointSize : ScreenTools.largeFontPointSize

            property string _commLostText:      qsTr("Communication Lost")
            property string _readyToFlyText:    qsTr("Ready To Fly")
            property string _notReadyToFlyText: qsTr("Not Ready")
            property string _disconnectedText:  qsTr("Disconnected")
            property string _armedText:         qsTr("Armed")
            property string _flyingText:        qsTr("Flying")
            property string _landingText:       qsTr("Landing")

            function mainStatusText() {
                var statusText
                if (_activeVehicle) {
                    if (_communicationLost) {
                        _mainStatusBGColor = "red"
                        return mainStatusLabel._commLostText
                    }
                    if (_activeVehicle.armed) {
                        _mainStatusBGColor = "green"

                        if (_healthAndArmingChecksSupported) {
                            if (_activeVehicle.healthAndArmingCheckReport.canArm) {
                                if (_activeVehicle.healthAndArmingCheckReport.hasWarningsOrErrors) {
                                    _mainStatusBGColor = "yellow"
                                }
                            } else {
                                _mainStatusBGColor = "red"
                            }
                        }

                        if (_activeVehicle.flying) {
                            return mainStatusLabel._flyingText
                        } else if (_activeVehicle.landing) {
                            return mainStatusLabel._landingText
                        } else {
                            return mainStatusLabel._armedText
                        }
                    } else {
                        if (_healthAndArmingChecksSupported) {
                            if (_activeVehicle.healthAndArmingCheckReport.canArm) {
                                if (_activeVehicle.healthAndArmingCheckReport.hasWarningsOrErrors) {
                                    _mainStatusBGColor = "yellow"
                                } else {
                                    _mainStatusBGColor = "green"
                                }
                                return mainStatusLabel._readyToFlyText
                            } else {
                                _mainStatusBGColor = "red"
                                return mainStatusLabel._notReadyToFlyText
                            }
                        } else if (_activeVehicle.readyToFlyAvailable) {
                            if (_activeVehicle.readyToFly) {
                                _mainStatusBGColor = "green"
                                return mainStatusLabel._readyToFlyText
                            } else {
                                _mainStatusBGColor = "yellow"
                                return mainStatusLabel._notReadyToFlyText
                            }
                        } else {
                            // Best we can do is determine readiness based on AutoPilot component setup and health indicators from SYS_STATUS
                            if (_activeVehicle.allSensorsHealthy && _activeVehicle.autopilot.setupComplete) {
                                _mainStatusBGColor = "green"
                                return mainStatusLabel._readyToFlyText
                            } else {
                                _mainStatusBGColor = "yellow"
                                return mainStatusLabel._notReadyToFlyText
                            }
                        }
                    }
                } else {
                    _mainStatusBGColor = Colors.dialogAccent
                    return mainStatusLabel._disconnectedText
                }
            }

            QGCMouseArea {
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                height:                 _root.height
                enabled:                _activeVehicle
                onClicked:              mainWindow.showIndicatorPopup(mainStatusLabel, sensorStatusInfoComponent)
            }
        }

        // Phase 7: Estimated flight time/range (visible when connected and not armed)
        QGCLabel {
            id:             estLabel
            visible:        _activeVehicle && !_activeVehicle.armed && !_communicationLost && _estRangeText.length > 0
            text:           _estRangeText
            font.pointSize: ScreenTools.smallFontPointSize
            color:          _estLabelColor
        }

        Timer {
            interval: 5000
            running: _activeVehicle && !_activeVehicle.armed && !_communicationLost
            repeat: true
            onTriggered: {
                updateEstimate()
            }
        }

        Component.onCompleted: updateEstimate()

        function updateEstimate() {
            if (!_activeVehicle || _activeVehicle.armed || _communicationLost) {
                _estRangeText = ""
                return
            }
            if (typeof PowerModel === 'undefined' || !PowerModel) {
                _estRangeText = ""
                return
            }
            var deviceUid = typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager
                ? VehicleProfileManager.currentDeviceUid : ""
            var payloadKg = typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager
                ? VehicleProfileManager.currentPayloadWeightKg : 0.0
            // Estimate capacity from battery voltage: rough Wh
            var voltage = _activeVehicle ? _activeVehicle.battery.voltage.value : 14.8
            var cellCount = Math.max(1, Math.round(voltage / 4.2))
            var capacityWh = cellCount * 5.0 * 3.7  // ~5000 mAh typical

            var est = PowerModel.estimateToMap(deviceUid, payloadKg, capacityWh, -1)
            if (!est) {
                _estRangeText = ""
                return
            }
            var range = est["rangeKm"]
            var flightTime = est["flightTimeMin"]
            var calibrated = est["isCalibrated"]
            var dataPts = est["dataPointCount"]

            if (calibrated) {
                _estLabelColor = Colors.success  // green
            } else if (dataPts > 0) {
                _estLabelColor = Colors.warning  // amber
            } else {
                _estLabelColor = Colors.textDisabled  // gray
            }

            _estRangeText = "Est. " + range.toFixed(1) + " km / " + flightTime.toFixed(0) + " min"
            if (calibrated) {
                _estRangeText += " (" + dataPts + " flights)"
            } else if (dataPts > 0) {
                _estRangeText += " (need " + (5 - dataPts) + " more flights)"
            } else {
                _estRangeText += " (default model)"
            }
        }
    }

    Item {
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * ScreenTools.largeFontPointRatio * 1.5
        height:                 1
    }

    FlightModeMenuIndicator {
        id:                     flightModeMenu
        Layout.preferredHeight: _root.height
        fontPointSize:          _vehicleInAir ?  ScreenTools.largeFontPointSize : ScreenTools.defaultFontPointSize
        visible:                _activeVehicle
    }

    Item {
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * ScreenTools.largeFontPointRatio * 1.5
        height:                 1
        visible:                vtolModeLabel.visible
    }

    QGCLabel {
        id:                     vtolModeLabel
        Layout.preferredHeight: _root.height
        verticalAlignment:      Text.AlignVCenter
        text:                   _vtolInFWDFlight ? qsTr("FW(vtol)") : qsTr("MR(vtol)")
        font.pointSize:         ScreenTools.largeFontPointSize
        visible:                _activeVehicle ? _activeVehicle.vtol && _vehicleInAir : false

        QGCMouseArea {
            anchors.fill:   parent
            onClicked:      mainWindow.showIndicatorPopup(vtolModeLabel, vtolTransitionComponent)
        }
    }

    Component {
        id: sensorStatusInfoComponent

        Rectangle {
            width:          flickable.width + (_margins * 2)
            height:         flickable.height + (_margins * 2)
            radius:         ScreenTools.defaultFontPixelHeight * 0.5
            color:          Colors.surface
            border.color:   Colors.textPrimary

            QGCFlickable {
                id:                 flickable
                anchors.margins:    _margins
                anchors.top:        parent.top
                anchors.left:       parent.left
                width:              mainLayout.width
                height:             mainWindow.contentItem.height - (indicatorPopup.padding * 2) - (_margins * 2)
                flickableDirection: Flickable.VerticalFlick
                contentHeight:      mainLayout.height
                contentWidth:       mainLayout.width

                ColumnLayout {
                    id:         mainLayout
                    spacing:    _spacing

                    Button {
                        id:                 armBtn
                        Layout.leftMargin:  _healthAndArmingChecksSupported ? width / 2 : 0
                        Layout.alignment:   _healthAndArmingChecksSupported ? Qt.AlignLeft : Qt.AlignHCenter
                        enabled:            _armed || (_activeVehicle !== null)
                        text:               _armed ?  qsTr("Disarm") : qsTr("Arm")

                        property bool forceArm: false

                        onPressAndHold: forceArm = true

                        contentItem: Text {
                            text: armBtn.text
                            font.pixelSize: ScreenTools.defaultFontPixelSize
                            font.bold: true
                            font.letterSpacing: 0.5
                            color: armBtn.enabled ? Colors.dialogText : Colors.textDisabled
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: 160
                            implicitHeight: 36
                            radius: 18
                            color: {
                                if (!armBtn.enabled) return Colors.surfaceLight
                                if (armBtn.down) return Qt.darker(_armed ? Colors.error : Colors.dialogAccent, 1.3)
                                return _armed ? Colors.error : Colors.dialogAccent
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                                height: 2; radius: 1; color: Qt.rgba(0,0,0,0.15)
                            }
                        }

                        onClicked: {
                            if (_armed) {
                                mainWindow.disarmVehicleRequest()
                                forceArm = false
                                mainWindow.hideIndicatorPopup()
                                return
                            }
                            if (forceArm) {
                                if (typeof ArmingGate !== 'undefined' && ArmingGate) {
                                    ArmingGate.forceArm()
                                }
                                mainWindow.armVehicleRequest()
                                forceArm = false
                                mainWindow.hideIndicatorPopup()
                                return
                            }
                            var criticalFails = typeof PreflightManager !== 'undefined' && PreflightManager ? PreflightManager.failedChecks : 0
                            var manualFails = typeof PreflightManager !== 'undefined' && PreflightManager ? PreflightManager.pendingChecks : 0
                            var decision = typeof ArmingGate !== 'undefined' && ArmingGate ? ArmingGate.processArmRequest(criticalFails, manualFails) : 0
                            if (decision === 1) {
                                _root._gateCriticalFails = criticalFails
                                _root._gateManualFails = manualFails
                                armGateDialogLoader.active = true
                            } else {
                                mainWindow.armVehicleRequest()
                                mainWindow.hideIndicatorPopup()
                            }
                            forceArm = false
                        }
                    }

                    QGCLabel {
                        Layout.alignment:   Qt.AlignHCenter
                        text:               qsTr("Sensor Status")
                        visible:            !_healthAndArmingChecksSupported
                    }

                    GridLayout {
                        rowSpacing:     _spacing
                        columnSpacing:  _spacing
                        rows:           _activeVehicle.sysStatusSensorInfo.sensorNames.length
                        flow:           GridLayout.TopToBottom
                        visible:        !_healthAndArmingChecksSupported

                        Repeater {
                            model: _activeVehicle.sysStatusSensorInfo.sensorNames

                            QGCLabel {
                                text: modelData
                            }
                        }

                        Repeater {
                            model: _activeVehicle.sysStatusSensorInfo.sensorStatus

                            QGCLabel {
                                text: modelData
                            }
                        }
                    }


                    QGCLabel {
                        text:               qsTr("Arming Check Report:")
                        visible:            _healthAndArmingChecksSupported && _activeVehicle.healthAndArmingCheckReport.problemsForCurrentMode.count > 0
                    }
                    // List health and arming checks
                    QGCListView {
                        visible:            _healthAndArmingChecksSupported
                        anchors.margins:    ScreenTools.defaultFontPixelHeight
                        spacing:            ScreenTools.defaultFontPixelWidth
                        width:              mainWindow.width * 0.66666
                        height:             contentHeight
                        model:              _activeVehicle ? _activeVehicle.healthAndArmingCheckReport.problemsForCurrentMode : null
                        delegate:           listdelegate
                    }

                    FactPanelController {
                        id: controller
                    }

                    Component {
                        id: listdelegate

                        Column {
                            width:      parent ? parent.width : 0
                            Row {
                                width:  parent.width
                                QGCLabel {
                                    id:           message
                                    text:         object.message
                                    wrapMode:     Text.WordWrap
                                    textFormat:   TextEdit.RichText
                                    width:        parent.width - arrowDownIndicator.width
                                    color:        object.severity == 'error' ? Colors.error : object.severity == 'warning' ? Colors.warning : Colors.textPrimary
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (object.description != "")
                                                object.expanded = !object.expanded
                                        }
                                    }
                                }

                                QGCColoredImage {
                                    id:                     arrowDownIndicator
                                    height:                 1.5 * ScreenTools.defaultFontPixelWidth
                                    width:                  height
                                    source:                 "/qmlimages/arrow-down.png"
                                    color:                  Colors.textPrimary
                                    visible:                object.description != ""
                                    MouseArea {
                                        anchors.fill:       parent
                                        onClicked:          object.expanded = !object.expanded
                                    }
                                }
                            }
                            Rectangle {
                                property var margin:      ScreenTools.defaultFontPixelWidth
                                id:                       descriptionRect
                                width:                    parent.width
                                height:                   description.height + margin
                                color:                    Colors.surfaceLight
                                visible:                  false
                                Connections {
                                    target:               object
                                    function onExpandedChanged() {
                                        if (object.expanded) {
                                            description.height = description.preferredHeight
                                        } else {
                                            description.height = 0
                                        }
                                    }
                                }

                                Behavior on height {
                                    NumberAnimation {
                                        id: animation
                                        duration: 150
                                        onRunningChanged: {
                                            descriptionRect.visible = animation.running || object.expanded
                                        }
                                    }
                                }
                                QGCLabel {
                                    id:                 description
                                    anchors.centerIn:   parent
                                    width:              parent.width - parent.margin * 2
                                    height:             0
                                    text:               object.description
                                    textFormat:         TextEdit.RichText
                                    wrapMode:           Text.WordWrap
                                    clip:               true
                                    property var fact:  null
                                    onLinkActivated: {
                                        if (link.startsWith('param://')) {
                                            var paramName = link.substr(8);
                                            fact = controller.getParameterFact(-1, paramName, true)
                                            if (fact != null) {
                                                paramEditorDialogComponent.createObject(mainWindow).open()
                                            }
                                        } else {
                                            Qt.openUrlExternally(link);
                                        }
                                    }
                                }

                                Component {
                                    id: paramEditorDialogComponent

                                    ParameterEditorDialog {
                                        title:          qsTr("Edit Parameter")
                                        fact:           description.fact
                                        destroyOnClose: true
                                    }
                                }
                            }
                        }
                    }

                }
            }
        }
    }

    Component {
        id: vtolTransitionComponent

        Rectangle {
            width:          mainLayout.width   + (_margins * 2)
            height:         mainLayout.height  + (_margins * 2)
            radius:         ScreenTools.defaultFontPixelHeight * 0.5
            color:          Colors.surface
            border.color:   Colors.textPrimary

            QGCButton {
                id:                 mainLayout
                anchors.margins:    _margins
                anchors.top:        parent.top
                anchors.left:       parent.left
                text:               _vtolInFWDFlight ? qsTr("Transition to Multi-Rotor") : qsTr("Transition to Fixed Wing")

                onClicked: {
                    if (_vtolInFWDFlight) {
                        mainWindow.vtolTransitionToMRFlightRequest()
                    } else {
                        mainWindow.vtolTransitionToFwdFlightRequest()
                    }
                    mainWindow.hideIndicatorPopup()
                }
            }
        }
    }
}

