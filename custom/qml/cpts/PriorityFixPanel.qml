import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

ColumnLayout {
    id: root
    spacing: Config.spacingSmall

    readonly property var catNames: [
        "Propulsion", "Power", "Navigation", "Communication",
        "Airframe", "Safety", "Environment", "Arming Gate"
    ]

    function catName(id) { return id >= 0 && id < 8 ? catNames[id] : "?" }

    Repeater {
        id: listView
        model: PreflightManager.blockingChecks

        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property var chk: modelData
            readonly property int cStatus: chk ? chk.status : 0
            readonly property bool isFailed: cStatus === 2 || cStatus === 4
            readonly property bool isWarning: cStatus === 3

            Layout.fillWidth: true
            Layout.preferredHeight: 48
            radius: Config.radiusSmall
            color: isFailed ? Colors.errorDim : isWarning ? Colors.checkWarnDim : Colors.surface
            border.color: isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.borderLight
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                spacing: Config.spacingSmall

                // Priority badge
                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 12
                    color: isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.textDisabled
                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        font.pixelSize: 10
                        font.bold: true
                        color: Colors.background
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall
                        Text {
                            text: chk ? chk.label : ""
                            font.pixelSize: Config.fontSizeSmall
                            font.bold: true
                            color: Colors.textPrimary
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        // Category tag
                        Rectangle {
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: catName(chk ? chk.checkCategory : 0).length * 7 + 10
                            radius: Config.radiusSmall
                            color: Colors.surfaceLight
                            border.color: Colors.borderLight
                            border.width: 1
                            visible: chk
                            Text {
                                anchors.centerIn: parent
                                text: catName(chk ? chk.checkCategory : 0)
                                font.pixelSize: 8
                                color: Colors.textSecondary
                            }
                        }
                        // Status badge
                        Rectangle {
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 30
                            radius: 3
                            color: isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.textDisabled
                            Text {
                                anchors.centerIn: parent
                                text: isFailed ? "FAIL" : isWarning ? "WARN" : "PEND"
                                font.pixelSize: 7
                                font.bold: true
                                color: Colors.background
                            }
                        }
                    }
                    Text {
                        text: {
                            if (!chk) return ""
                            var m = chk.message
                            return m.length > 0 ? m : (chk.getRecommendedAction ? chk.getRecommendedAction() : "")
                        }
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }
                }

                // Action button
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: 56
                    radius: Config.radiusSmall
                    color: Colors.surfaceLight
                    border.color: Colors.border
                    border.width: 1
                    visible: chk

                    Text {
                        anchors.centerIn: parent
                        text: chk && chk.type === 1 ? "Confirm" : chk && chk.type === 2 ? "Run" : "Check"
                        font.pixelSize: 10
                        font.bold: true
                        color: Colors.textPrimary
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (!chk) return
                            if (chk.type === 1) {
                                chk.confirm("Operator confirmed via priority fix list")
                            } else if (chk.type === 2) {
                                chk.evaluate()
                                if (chk.status !== 1) chk.confirm("Action completed via priority fix list")
                            } else {
                                chk.evaluate()
                            }
                        }
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }
    }

    // Footer: quick actions
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        color: Colors.surfaceLight
        radius: Config.radiusSmall
        border.color: Colors.border
        border.width: 1
        visible: PreflightManager.blockingCount > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Config.spacingSmall
            anchors.rightMargin: Config.spacingSmall
            spacing: Config.spacingSmall

            Text {
                text: "Quick Actions:"
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
                color: Colors.textSecondary
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: 80
                radius: Config.radiusSmall
                color: Colors.successDim
                border.color: Colors.success
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "Run All Auto"
                    font.pixelSize: 9
                    font.bold: true
                    color: Colors.success
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: PreflightManager.evaluateAll()
                    cursorShape: Qt.PointingHandCursor
                }
            }

            Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: 100
                radius: Config.radiusSmall
                color: Colors.warning
                border.color: Colors.warning
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "Override Gate"
                    font.pixelSize: 9
                    font.bold: true
                    color: Colors.background
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: ArmingGate.overrideGate("Operator override from priority fix list")
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
