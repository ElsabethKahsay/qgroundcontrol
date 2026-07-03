/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactSystem
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools


Rectangle {
    id:             topRightPanel
    width:          contentWidth
    height:         Math.max(contentHeight, minimumHeight)
    color:          qgcPal.toolbarBackground
    radius:         ScreenTools.defaultFontPixelHeight / 2
    visible:        !QGroundControl.videoManager.fullScreen && _multipleVehicles && _settingEnableMVPanel
    clip:           true

    property bool _settingEnableMVPanel:    QGroundControl.settingsManager.appSettings.enableMultiVehiclePanel.value
    property bool  _multipleVehicles:       QGroundControl.multiVehicleManager.vehicles.count > 1
    property var   vehicles:                QGroundControl.multiVehicleManager.vehicles
    property var   selectedVehicles:        QGroundControl.multiVehicleManager.selectedVehicles
    property real  contentWidth:            Math.max(
                                                multiVehicleList.implicitWidth,
                                                contentColumnLayout.implicitWidth
                                            ) + ScreenTools.defaultFontPixelHeight
    property real  contentHeight:           Math.min(
                                                maximumHeight,
                                                topRightPanelColumnLayout.implicitHeight + topRightPanelColumnLayout.spacing * ( topRightPanelColumnLayout.children.length - 1)
                                            )
    property real  minimumHeight:           selectedVehiclesLabel.height + contentColumnLayout.height
    property real  maximumHeight

    QGCPalette { id: qgcPal }

    DeadMouseArea {
        anchors.fill:       parent
    }

    ColumnLayout {
        id:                 topRightPanelColumnLayout
        anchors.fill:       parent
        anchors.margins:    ScreenTools.defaultFontPixelHeight / 2
        spacing:            ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            id: selectedVehiclesLabel
            text: {
                let ids = Array.from({length: selectedVehicles.count}, (_, i) =>
                    selectedVehicles.get(i).id
                ).sort((a, b) => a - b)
                .join(", ")
                return qsTr("Selected: ") + ids
            }
        }

        MultiVehicleList {
            id:                    multiVehicleList
            Layout.fillWidth:      true
            Layout.fillHeight:     true

            Rectangle {
                anchors.fill: parent
                visible:      topRightPanel.height === maximumHeight

                Rectangle {
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    0
                    height:             1
                    color:              QGroundControl.globalPalette.groupBorder
                }

                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.00; color: topRightPanel.color }
                    GradientStop { position: 0.05; color: "transparent" }

                    GradientStop { position: 0.95; color: "transparent" }
                    GradientStop { position: 1.00; color: topRightPanel.color }
                }

                Rectangle {
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.bottom:     parent.bottom
                    anchors.margins:    0
                    height:             1
                    color:              QGroundControl.globalPalette.groupBorder
                }
            }

        }

        ColumnLayout {
            id:                     contentColumnLayout
            Layout.fillWidth:       true
            anchors.left:           parent.left
            anchors.right:          parent.right
            spacing:                ScreenTools.defaultFontPixelHeight

            Loader {
                id:                         photoVideoControlLoader
                Layout.alignment:           Qt.AlignHCenter
                sourceComponent:            globals.activeVehicle ? photoVideoControlComponent : undefined

                property real rightEdgeCenterInset: visible ? parent.width - x : 0

                Component {
                    id: photoVideoControlComponent

                    PhotoVideoControl {
                    }
                }
            }

            ColumnLayout {
                id:                     buttonsColumnLayout
                Layout.fillWidth:       true
                spacing:                ScreenTools.defaultFontPixelHeight / 2
                implicitHeight:         Math.max(selectionRowLayout.height, actionRowLayout.height) + ScreenTools.defaultFontPixelHeight * 2
                implicitWidth:          Math.max(selectionRowLayout.width, actionRowLayout.width) + ScreenTools.defaultFontPixelHeight * 4

                QGCLabel {
                    text:               qsTr("Multi Vehicle Selection")
                    Layout.alignment:   Qt.AlignHCenter
                }

                RowLayout {
                    id:                 selectionRowLayout
                    Layout.alignment:   Qt.AlignHCenter

                    QGCButton {
                        text:                  qsTr("Select All")
                        enabled:               multiVehicleList.selectedVehicles && multiVehicleList.selectedVehicles.count !== QGroundControl.multiVehicleManager.vehicles.count
                        onClicked:             multiVehicleList.selectAll()
                    }

                    QGCButton {
                        text:                  qsTr("Deselect All")
                        enabled:               multiVehicleList.selectedVehicles && multiVehicleList.selectedVehicles.count > 0
                        onClicked:             multiVehicleList.deselectAll()
                    }
                }

                QGCLabel {
                    text:              qsTr("Multi Vehicle Actions")
                    Layout.alignment:  Qt.AlignHCenter
                }

                RowLayout {
                    id:                actionRowLayout
                    Layout.alignment:  Qt.AlignHCenter

                    QGCButton {
                        text:                  qsTr("Arm")
                        enabled:               multiVehicleList.armAvailable()
                        onClicked:             _guidedController.confirmAction(_guidedController.actionMVArm)
                        Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.75
                        leftPadding:           0
                        rightPadding:          0
                    }

                    QGCButton {
                        text:                  qsTr("Disarm")
                        enabled:               multiVehicleList.disarmAvailable()
                        onClicked:             _guidedController.confirmAction(_guidedController.actionMVDisarm)
                        Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.75
                        leftPadding:           0
                        rightPadding:          0
                    }

                    QGCButton {
                        text:                  qsTr("Start")
                        enabled:               multiVehicleList.startAvailable()
                        onClicked:             _guidedController.confirmAction(_guidedController.actionMVStartMission)
                        Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.75
                        leftPadding:           0
                        rightPadding:          0
                    }

                    QGCButton {
                        text:                  qsTr("Pause")
                        enabled:               multiVehicleList.pauseAvailable()
                        onClicked:             _guidedController.confirmAction(_guidedController.actionMVPause)
                        Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.75
                        leftPadding:           0
                        rightPadding:          0
                    }
                }
            }
        }
    }
}

