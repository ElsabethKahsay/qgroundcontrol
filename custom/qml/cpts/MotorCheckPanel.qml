import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

// ── Motor Test Launcher ──────────────────────────────────────────────────────
// Inline placeholder: compact single row shown inside checklist card.
// Full per-motor UI is inside motorDialog (modal, dialog-based).
Rectangle {
    id: root

    property var checklistCheck

    // Compact height for inline row — kept aligned with sibling action cards
    // (collapsed action card height is 72 incl. the loader's 10px padding).
    implicitHeight: Math.max(launcherRow.implicitHeight + Config.spacingMedium * 2, 62)
    radius: Config.radiusMedium
    color: Colors.surface
    border.color: Colors.border
    border.width: 1

    readonly property int    _mc:    HardwareTestController.motorCount
    readonly property string _vt:    HardwareTestController.vehicleTypeLabel
    readonly property bool   _armed: HardwareTestController.isArmed

    function openDialog() { motorDialog.open() }
    function testLabel(index) {
        if (HardwareTestController.isFixedWing && index === 1) {
            return HardwareTestController.fixedWingTestLabel()
        }
        return "M" + index
    }

    // Click anywhere on the tile to open the dialog (won't interfere with button)
    MouseArea {
        anchors.fill: parent
        z: -1
        cursorShape: Qt.PointingHandCursor
        onClicked: motorDialog.open()
    }

    // ── Inline launcher row ──────────────────────────────────────────────────
    RowLayout {
        id: launcherRow
        anchors { fill: parent; margins: Config.spacingMedium }
        spacing: Config.spacingMedium

        // Motor count badge
        Rectangle {
            width: 36; height: 36; radius: 18
            color: root._mc > 0 ? Colors.accentDim : Colors.surfaceLight
            border.color: root._mc > 0 ? Colors.accent : Colors.border
            border.width: 1
            Behavior on color { ColorAnimation { duration: 200 } }

            Text {
                anchors.centerIn: parent
                text: root._mc > 0 ? root._mc : "?"
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: root._mc > 0 ? Colors.accent : Colors.textDisabled
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: qsTr("Motor Test")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root._mc > 0
                    ? (root._vt.length > 0 ? root._vt : qsTr("Vehicle")) + " \u2014 " + root._mc
                      + " " + qsTr("motor") + (root._mc !== 1 ? "s" : "") + qsTr(" detected")
                    : qsTr("Waiting for vehicle parameters\u2026")
                font.pixelSize: Config.fontSizeSmall
                color: root._mc > 0 ? Colors.textSecondary : Colors.textDisabled
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // Armed warning
        Rectangle {
            visible: root._armed
            Layout.preferredHeight: 24
            Layout.preferredWidth: armedTxt.implicitWidth + 12
            radius: Config.radiusSmall
            color: Colors.errorDim
            border.color: Colors.error
            border.width: 1
            Text { id: armedTxt; anchors.centerIn: parent; text: "\u26A0 DISARM"; font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.error }
        }

        // Open dialog button — width follows its label so the text can never clip
        Rectangle {
            id: openBtn
            Layout.preferredHeight: 34
            Layout.preferredWidth: Math.max(openBtnTxt.implicitWidth + 24, 120)
            radius: Config.radiusSmall
            color: Colors.accent
            Behavior on color { ColorAnimation { duration: 150 } }

            Text {
                id: openBtnTxt
                anchors.centerIn: parent
                text: qsTr("Open Motor Test")
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
                color: Colors.background
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                enabled: true
                onClicked: motorDialog.open()
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // MOTOR TEST DIALOG
    // ════════════════════════════════════════════════════════════════════════
    Dialog {
        id: motorDialog
        parent: Overlay.overlay
        anchors.centerIn: parent

        width: {
            if (root._mc <= 4) return 480
            if (root._mc <= 6) return 600
            return 720
        }

        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 0; topPadding: 0; bottomPadding: 0

        // Auto-reset on close
        onClosed: HardwareTestController.stopAll()

        background: Rectangle {
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1
        }

        // ── Draggable title bar ──────────────────────────────────────────
        header: Rectangle {
            height: 48
            color: Colors.surfaceLight
            // Square bottom edge so border meets content cleanly
            radius: 0

            // Top-only radius via background clip
            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: Config.radiusMedium
                color: parent.color
            }

            // Drag
            property real _ox: 0; property real _oy: 0
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                onPressed: { parent._ox = mouseX; parent._oy = mouseY }
                onPositionChanged: if (pressed) {
                    motorDialog.x = Math.max(0, Math.min(motorDialog.parent.width - motorDialog.width, motorDialog.x + mouseX - parent._ox))
                    motorDialog.y = Math.max(0, Math.min(motorDialog.parent.height - motorDialog.height, motorDialog.y + mouseY - parent._oy))
                }
            }

            RowLayout {
                anchors { fill: parent; leftMargin: Config.spacingMedium; rightMargin: Config.spacingSmall }
                spacing: Config.spacingSmall

                Text { text: "\u2699"; font.pixelSize: 18; color: Colors.accent }

                Text {
                    Layout.fillWidth: true
                    text: {
                        var s = qsTr("Motor Test")
                        if (root._vt.length > 0) s += " \u2014 " + root._vt
                        if (root._mc > 0) s += " (" + root._mc + " " + qsTr("motors") + ")"
                        else s += " \u2014 " + qsTr("Detecting\u2026")
                        return s
                    }
                    font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: 28; height: 28; radius: 4
                    color: xHover.containsMouse ? Colors.errorDim : "transparent"
                    border.color: xHover.containsMouse ? Colors.error : "transparent"
                    border.width: 1
                    Behavior on color { ColorAnimation { duration: 100 } }
                    Text { anchors.centerIn: parent; text: "\u2715"; font.pixelSize: 13; color: Colors.textSecondary }
                    MouseArea { id: xHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: motorDialog.close() }
                }
            }
        }

        // ── Dialog content ───────────────────────────────────────────────
        contentItem: ScrollView {
            id: dialogScroll
            implicitHeight: Math.min(dialogCol.implicitHeight + Config.spacingMedium * 2,
                                     motorDialog.parent ? motorDialog.parent.height * 0.72 : 520)
            contentWidth: availableWidth
            leftPadding: Config.spacingMedium
            rightPadding: Config.spacingMedium
            topPadding: Config.spacingMedium
            bottomPadding: Config.spacingMedium
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            clip: true

            ColumnLayout {
                id: dialogCol
                width: dialogScroll.availableWidth
                spacing: Config.spacingMedium

                // ── MANUAL mode + RC override (fixed wing / VTOL only) ──
                RowLayout {
                    Layout.fillWidth: true
                    visible: VehicleProfileManager.vehicleKind === "FIXED_WING"
                          || VehicleProfileManager.vehicleKind === "VTOL_CONVENTIONAL"
                    spacing: Config.spacingMedium

                    Button {
                        id: manualModeBtn
                        property bool _inManual: {
                            var v = QGroundControl.multiVehicleManager.activeVehicle
                            return v ? v.flightMode === "MANUAL" : false
                        }
                        Layout.preferredHeight: 34
                        text: _inManual ? qsTr("✓ MANUAL Mode") : qsTr("Switch to MANUAL")
                        enabled: !!QGroundControl.multiVehicleManager.activeVehicle && !_inManual
                        onClicked: {
                            var v = QGroundControl.multiVehicleManager.activeVehicle
                            if (v && typeof v.setFlightMode === "function") {
                                v.setFlightMode("MANUAL")
                            }
                            if (typeof ControlSurfaceTestController !== "undefined") {
                                ControlSurfaceTestController.switchToManual()
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: manualModeBtn._inManual ? "#1A3A2A" : Colors.warning
                            border.color: manualModeBtn._inManual ? Colors.statePass : Colors.warning
                            border.width: 1
                        }
                        contentItem: Text {
                            text: manualModeBtn.text
                            font.pixelSize: Config.fontSizeSmall
                            font.bold: true
                            color: manualModeBtn._inManual ? Colors.statePass : Colors.background
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.text: qsTr("Motor and surface tests require MANUAL flight mode")
                        ToolTip.visible: hovered
                    }

                    Label {
                        text: qsTr("RC Override:")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Switch {
                        id: rcOverrideSwitch
                        enabled: !!QGroundControl.multiVehicleManager.activeVehicle
                        checked: HardwareTestController.rcOverrideActive
                        onToggled: HardwareTestController.setRcOverrideActive(checked)
                        ToolTip.text: checked
                            ? qsTr("RC override ON — attitude control neutralized for testing")
                            : qsTr("RC override OFF — normal RC input active")
                        ToolTip.visible: hovered
                    }
                    Item { Layout.fillWidth: true }
                }

                // ── LOADING STATE ────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root._mc <= 0
                    spacing: Config.spacingMedium
                    Layout.topMargin: Config.spacingLarge
                    Layout.bottomMargin: Config.spacingLarge

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: root._mc <= 0
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Detecting vehicle configuration\u2026\nEnsure the vehicle is connected and parameters are loaded.")
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                // ── ARMED OVERLAY (when dialog is open but user armed) ───
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    visible: root._armed && root._mc > 0
                    radius: Config.radiusSmall
                    color: Colors.errorDim
                    border.color: Colors.error
                    border.width: 1
                    RowLayout {
                        anchors { fill: parent; margins: Config.spacingSmall }
                        spacing: Config.spacingSmall
                        Text { text: "\u26A0"; font.pixelSize: 18; color: Colors.error }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Vehicle is armed — disarm before running motor tests")
                            font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.error
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: 90
                            radius: Config.radiusSmall
                            color: Colors.error
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Disarm")
                                font.pixelSize: Config.fontSizeSmall; font.bold: true; color: "white"
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: HardwareTestController.disarmVehicle()
                            }
                        }
                    }
                }

                // ── SAFETY BANNER ────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    visible: root._mc > 0 && !root._armed
                    Layout.preferredHeight: safetyTxt.implicitHeight + 16
                    radius: Config.radiusSmall
                    color: Colors.warningDim
                    border.color: Colors.warning; border.width: 1
                    RowLayout {
                        anchors { fill: parent; margins: Config.spacingSmall }
                        spacing: Config.spacingSmall
                        Text { text: "\u26A0"; font.pixelSize: 13; color: Colors.warning }
                        Text {
                            id: safetyTxt
                            Layout.fillWidth: true
                            text: qsTr("Vehicle stays disarmed during the motor test.\nRemove propeller or ensure the area is clear before starting.")
                            font.pixelSize: Config.fontSizeSmall; color: Colors.warning
                            wrapMode: Text.WordWrap
                        }
                    }
                }


                // ── SHARED DURATION ──────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    visible: root._mc > 0
                    spacing: Config.spacingSmall

                    Text { text: qsTr("Duration:"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; Layout.preferredWidth: 64 }
                    Text {
                        text: HardwareTestController.durationSec + "s"
                        font.pixelSize: Config.fontSizeSmall
                        font.bold: true
                        color: Colors.textPrimary
                        Layout.preferredWidth: 28
                    }

                    Slider {
                        id: durSlider
                        Layout.fillWidth: true
                        from: 1; to: 10; stepSize: 1
                        value: HardwareTestController.durationSec
                        enabled: HardwareTestController.activeMotor === -1
                                 && !HardwareTestController.isArmed
                                 && !root._armed
                        // onMoved (not onValueChanged) + re-bind afterwards so the
                        // C++ property remains the source of truth in both directions.
                        onMoved: {
                            HardwareTestController.setDurationSec(Math.round(value))
                            value = Qt.binding(function() { return HardwareTestController.durationSec })
                        }

                        background: Rectangle {
                            x: durSlider.leftPadding
                            y: durSlider.topPadding + durSlider.availableHeight / 2 - height / 2
                            width: durSlider.availableWidth; height: 4; radius: 2; color: Colors.borderLight
                            Rectangle { width: durSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: Colors.info }
                        }
                        handle: Rectangle {
                            x: durSlider.leftPadding + durSlider.visualPosition * (durSlider.availableWidth - width)
                            y: durSlider.topPadding + durSlider.availableHeight / 2 - height / 2
                            width: 16; height: 16; radius: 8
                            color: durSlider.pressed ? Colors.info : Colors.textPrimary
                            border.color: Colors.border; border.width: 1
                        }
                    }

                    Text {
                        text: Math.round(durSlider.value) + "s"
                        font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.textPrimary
                        Layout.preferredWidth: 28
                    }
                }

                // ── MOTOR CARD GRID ──────────────────────────────────────
                GridLayout {
                    Layout.fillWidth: true
                    visible: root._mc > 0
                    columns: root._mc <= 4 ? 2 : (root._mc <= 6 ? 3 : 4)
                    rowSpacing: Config.spacingSmall
                    columnSpacing: Config.spacingSmall

                    Repeater {
                        model: root._mc > 0 ? root._mc : 0

                        // ── Per-motor card ───────────────────────────────
                        Rectangle {
                            id: mcard
                            required property int index
                            readonly property int midx: index + 1
                            readonly property int st: HardwareTestController.motorStatus(midx)
                            readonly property int fb: HardwareTestController.motorFeedback(midx)

                            Layout.fillWidth: true
                            implicitHeight: mc.implicitHeight + 16
                            radius: Config.radiusSmall
                            border.width: 2
                            border.color: st === HardwareTestController.Testing  ? Colors.testing
                                        : st === HardwareTestController.Pass     ? Colors.success
                                        : st === HardwareTestController.Fail     ? Colors.error
                                        : st === HardwareTestController.Cooldown ? Colors.borderLight
                                        : Colors.border
                            color: st === HardwareTestController.Pass     ? Colors.successDim
                                 : st === HardwareTestController.Fail     ? Colors.errorDim
                                 : st === HardwareTestController.Cooldown ? Colors.surfaceLight
                                 : Colors.surfaceLight
                            Behavior on border.color { ColorAnimation { duration: 200 } }
                            Behavior on color       { ColorAnimation { duration: 200 } }

                            ColumnLayout {
                                id: mc
                                anchors { fill: parent; margins: 8 }
                                spacing: 6

                                // Header row
                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: root.testLabel(mcard.midx)
                                        font.pixelSize: Config.fontSizeBody; font.bold: true
                                        color: Colors.textPrimary
                                    }

                                    Text {
                                        text: { var p = HardwareTestController.motorPosition(mcard.midx); return p.length > 0 ? "(" + p + ")" : "" }
                                        font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary
                                        visible: text.length > 0
                                    }

                                    Item { Layout.fillWidth: true }

                                    // Status chip
                                    Rectangle {
                                        visible: mcard.st !== HardwareTestController.Idle
                                        Layout.preferredHeight: 18
                                        Layout.preferredWidth: chipTxt.implicitWidth + 10
                                        radius: 9
                                        color: mcard.st === HardwareTestController.Testing  ? Colors.testing
                                             : mcard.st === HardwareTestController.Pass     ? Colors.success
                                             : mcard.st === HardwareTestController.Fail     ? Colors.error
                                             : mcard.st === HardwareTestController.Cooldown ? Colors.border
                                             : "transparent"
                                        Text {
                                            id: chipTxt
                                            anchors.centerIn: parent
                                            font.pixelSize: 9; font.bold: true; color: Colors.background
                                            text: mcard.st === HardwareTestController.Testing  ? "Testing\u2026"
                                                : mcard.st === HardwareTestController.Pass     ? "Pass " + mcard.fb + "\u00b5s"
                                                : mcard.st === HardwareTestController.Fail     ? (mcard.fb === 0 ? "No signal" : "Mismatch")
                                                : mcard.st === HardwareTestController.Cooldown ? "Cooldown\u2026"
                                                : ""
                                        }
                                    }
                                }

                                // PWM slider row
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 4

                                    Text { text: "PWM:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; Layout.preferredWidth: 36 }

                                    Slider {
                                        id: pwmSl
                                        Layout.fillWidth: true
                                        from: 1000; to: HardwareTestController.isFixedWing ? 1800 : 1200; stepSize: 10
                                        value: (HardwareTestController.motorPwmValues.length > mcard.index)
                                               ? HardwareTestController.motorPwmValues[mcard.index] : 1100
                                        enabled: !root._armed && HardwareTestController.activeMotor === -1
                                        // onMoved + re-bind keeps C++ ↔ QML in sync after drags
                                        onMoved: {
                                            HardwareTestController.setMotorPwm(mcard.midx, Math.round(value))
                                            value = Qt.binding(function() {
                                                return (HardwareTestController.motorPwmValues.length > mcard.index)
                                                       ? HardwareTestController.motorPwmValues[mcard.index] : 1100
                                            })
                                        }

                                        background: Rectangle {
                                            x: pwmSl.leftPadding; y: pwmSl.topPadding + pwmSl.availableHeight / 2 - height / 2
                                            width: pwmSl.availableWidth; height: 4; radius: 2; color: Colors.borderLight
                                            Rectangle { width: pwmSl.visualPosition * parent.width; height: parent.height; radius: 2; color: Colors.accent }
                                        }
                                        handle: Rectangle {
                                            x: pwmSl.leftPadding + pwmSl.visualPosition * (pwmSl.availableWidth - width)
                                            y: pwmSl.topPadding + pwmSl.availableHeight / 2 - height / 2
                                            width: 14; height: 14; radius: 7
                                            color: pwmSl.pressed ? Colors.accent : Colors.textPrimary
                                            border.color: Colors.border; border.width: 1
                                        }
                                    }

                                    Text {
                                        text: Math.round(pwmSl.value) + "\u00b5s"
                                        font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.textPrimary
                                        Layout.preferredWidth: 52
                                    }
                                }

                                // Test button
                                Rectangle {
                                    id: testBtn
                                    Layout.fillWidth: true; Layout.preferredHeight: 30
                                    radius: Config.radiusSmall
                                    property bool canTest: mcard.st !== HardwareTestController.Testing
                                                        && mcard.st !== HardwareTestController.Cooldown
                                                        && HardwareTestController.activeMotor < 0


                                    color: !canTest                                     ? Colors.borderLight
                                         : mcard.st === HardwareTestController.Pass    ? Colors.success
                                         : mcard.st === HardwareTestController.Fail    ? Colors.error
                                         : Colors.accent
                                    Behavior on color { ColorAnimation { duration: 150 } }

                                    SequentialAnimation on opacity {
                                        loops: Animation.Infinite; running: mcard.st === HardwareTestController.Testing
                                        NumberAnimation { from: 1.0; to: 0.35; duration: 380 }
                                        NumberAnimation { from: 0.35; to: 1.0; duration: 380 }
                                    }
                                    onCanTestChanged: if (canTest) opacity = 1.0

                                    Text {
                                        anchors.centerIn: parent
                                        text: mcard.st === HardwareTestController.Testing  ? "TESTING\u2026"
                                            : mcard.st === HardwareTestController.Pass     ? "\u2713 PASS"
                                            : mcard.st === HardwareTestController.Fail     ? "\u2717 FAIL"
                                            : mcard.st === HardwareTestController.Cooldown ? "WAIT\u2026"
                                            : "TEST " + root.testLabel(mcard.midx)
                                        font.pixelSize: Config.fontSizeSmall; font.bold: true
                                        color: testBtn.canTest ? Colors.background : Colors.textDisabled
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: testBtn._canTest
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                                        onClicked: {
                                            if (!HardwareTestController.firstTestDone) {
                                                safetyDialog.pendingMotor = mcard.midx
                                                safetyDialog.open()
                                            } else {
                                                HardwareTestController.testMotor(mcard.midx)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } // Repeater
                } // GridLayout

                // ── Cooldown indicator ───────────────────────────────────
                Text {
                    Layout.fillWidth: true
                    visible: root._mc > 0 && HardwareTestController.cooldownActive
                    text: qsTr("Cooldown: ") + (HardwareTestController.cooldownMsRemaining / 1000).toFixed(1) + "s"
                    font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                }

                // ── Summary + confirm ────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    visible: root._mc > 0
                    spacing: Config.spacingSmall

                    Text {
                        Layout.fillWidth: true
                        font.pixelSize: Config.fontSizeSmall; font.bold: true
                        color: Colors.success
                        text: {
                            var pass = 0, fail = 0
                            for (var i = 1; i <= root._mc; ++i) {
                                var s = HardwareTestController.motorStatus(i)
                                if (s === HardwareTestController.Pass) pass++
                                else if (s === HardwareTestController.Fail) fail++
                            }
                            if (pass + fail === 0) return ""
                            return pass + "/" + root._mc + qsTr(" passed") + (fail > 0 ? "  (" + fail + qsTr(" failed)") : "")
                        }
                    }

                    Button {
                        text: qsTr("Reset")
                        font.pixelSize: Config.fontSizeSmall
                        Layout.preferredHeight: 30; Layout.preferredWidth: 68
                        onClicked: { HardwareTestController.resetAll(); root.checklistCheck.reset() }
                    }

                    Button {
                        text: qsTr("Confirm Pass")
                        highlighted: true
                        font.pixelSize: Config.fontSizeSmall
                        Layout.preferredHeight: 30; Layout.preferredWidth: 100
                        enabled: {
                            for (var i = 1; i <= root._mc; ++i)
                                if (HardwareTestController.motorStatus(i) === HardwareTestController.Pass) return true
                            return false
                        }
                        onClicked: { root.checklistCheck.confirm(qsTr("Motor test completed")); motorDialog.close() }
                    }
                }
            }
        }

        // ── STOP ALL footer ──────────────────────────────────────────────
        footer: Rectangle {
            height: 48
            color: Colors.surfaceLight
            radius: 0
            visible: root._mc > 0

            // Square top edge
            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: Config.radiusMedium; color: parent.color
            }
            // Bottom rounded corners via background clip
            Rectangle {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: Config.radiusMedium; color: parent.color
            }

            RowLayout {
                anchors { fill: parent; margins: Config.spacingSmall }
                spacing: Config.spacingSmall

                // Active motor indicator
                Text {
                    visible: HardwareTestController.activeMotor > 0
                    text: qsTr("Testing ") + root.testLabel(HardwareTestController.activeMotor) + "\u2026"
                    font.pixelSize: Config.fontSizeSmall; color: Colors.testing; font.bold: true
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true; visible: HardwareTestController.activeMotor <= 0 }

                Rectangle {
                    Layout.preferredHeight: 34; Layout.preferredWidth: 120
                    radius: Config.radiusSmall; color: Colors.errorDim

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite; running: HardwareTestController.activeMotor > 0
                        NumberAnimation { from: 1.0; to: 0.6; duration: 500 }
                        NumberAnimation { from: 0.6; to: 1.0; duration: 500 }
                    }

                    Text {
                        anchors.centerIn: parent; text: "\u2B1B " + qsTr("STOP ALL")
                        font.pixelSize: Config.fontSizeSmall; font.bold: true; color: "white"
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: HardwareTestController.stopAll() }
                }

                Button {
                    text: qsTr("Close")
                    font.pixelSize: Config.fontSizeSmall
                    Layout.preferredHeight: 34; Layout.preferredWidth: 72
                    onClicked: motorDialog.close()
                }
            }
        }
    }

    // ── First-test safety confirmation dialog ────────────────────────────────
    Dialog {
        id: safetyDialog
        property int pendingMotor: -1
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Confirm Motor Test")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        width: 340

        background: Rectangle { color: Colors.surface; radius: Config.radiusMedium; border.color: Colors.border; border.width: 1 }
        header: Label { text: safetyDialog.title; font.bold: true; color: Colors.textPrimary; padding: Config.spacingMedium }

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingMedium

            Text {
                Layout.fillWidth: true
                text: root.testLabel(safetyDialog.pendingMotor) + qsTr(" will spin at ")
                      + ((HardwareTestController.motorPwmValues.length >= safetyDialog.pendingMotor)
                         ? HardwareTestController.motorPwmValues[safetyDialog.pendingMotor - 1] : 1100)
                      + "\u00b5s for " + HardwareTestController.durationSec + qsTr(" seconds.")
                font.pixelSize: Config.fontSizeBody; color: Colors.textPrimary
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Ensure propellers are removed or the area is clear.")
                font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.warning
                wrapMode: Text.WordWrap
            }
        }

        onAccepted: {
            HardwareTestController.firstTestDone = true
            if (pendingMotor > 0) HardwareTestController.testMotor(pendingMotor)
            pendingMotor = -1
        }
        onRejected: pendingMotor = -1
    }
}
