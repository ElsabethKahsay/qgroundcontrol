// PreFlightChecklist.qml
// Purpose: 5-step wizard shell for the Skywin GCS preflight process.
//   Manages transitions between:
//     Step 0 — Operator Selection  (WizardStep1Operator)
//     Step 1 — System Preflight   (existing checklist + 30% WeatherPanel sidebar)
//     Step 2 — Hardware Checks    (WizardStep3Hardware)
//     Step 3 — Final Review       (WizardStep4Review)
//
// The WizardStepBar at the top always reflects the current active step.
// Step transitions animate left/right (200ms ease-out).
// On vehicle disconnect, the wizard resets to Step 0.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0
import cpts 1.0 as CE

Page {
    id: root

    background: Rectangle { color: Colors.background }

    // ── Engine for system checks (Step 1) ───────────────────────────────────
    CE.ChecklistEngine {
        id: _engine
        viewMode: "standalone"
    }

    // ── Weather fetch on load ────────────────────────────────────────────────
    property real _lastFetchLat: 0
    property real _lastFetchLon: 0
    property real _lastFetchTime: 0

    Component.onCompleted: {
        _motorDialogTimer.start()
        if (typeof WeatherProvider !== "undefined" && TelemetryProvider) {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat !== 0 || lon !== 0)
                Qt.callLater(function() { WeatherProvider.fetchWeather(lat, lon) })
        }
    }

    Connections {
        target: TelemetryProvider
        function onGpsLatitudeChanged() {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat === 0 && lon === 0) return
            var now = Date.now()
            if (now - _lastFetchTime < 30000) return
            var dLat = lat - _lastFetchLat
            var dLon = lon - _lastFetchLon
            if (dLat * dLat + dLon * dLon < 0.0001) return
            _lastFetchLat = lat; _lastFetchLon = lon; _lastFetchTime = now
            WeatherProvider.fetchWeather(lat, lon)
        }
    }

    // ── Disconnect guard: reset to Step 0 ───────────────────────────────────
    Connections {
        target: TelemetryProvider
        function onIsConnectedChanged() {
            if (!TelemetryProvider.isConnected) {
                stackView.pop(null)  // pop to step 0
                _currentStep = 0
            }
        }
    }

    // ── Wizard state ─────────────────────────────────────────────────────────
    property int _currentStep: 0

    // Hardware results passed forward from Step 2 → Step 3
    property bool _cameraOk: false
    property bool _gimbalOk: false

    // Checklist collapsible groups (used by Step 1 system view)
    property var _collapsed: ({})
    function _clone(obj) { var c = {}; for (var k in obj) c[k] = obj[k]; return c }
    function toggleGroup(idx) { _collapsed[idx] = !_collapsed[idx]; _collapsed = _clone(_collapsed) }
    property int _highlightIndex: -1
    property string _highlightCheckId: ""

    // ── Flash timer for blocker highlight ────────────────────────────────────
    Timer {
        id: flashTimer
        interval: 1500; repeat: false
        onTriggered: { _highlightIndex = -1; _highlightCheckId = "" }
    }

    // ── Motor config dialog (shown if ArduPilot needs motor setup) ───────────
    Dialog {
        id: motorConfigDialog
        title: qsTr("ArduPilot Motor Setup")
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: parent
        width: Math.min(480, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.border; border.width: 1; radius: Config.radiusMedium }
        padding: Config.spacingMedium
        header: Label { text: motorConfigDialog.title; font.bold: true; color: Colors.accent; padding: Config.spacingMedium }
        Text {
            wrapMode: Text.Wrap; color: Colors.textPrimary; font.pixelSize: Config.fontSizeBody
            text: (typeof TelemetryProvider !== "undefined" && TelemetryProvider.motorConfigWarning
                    ? TelemetryProvider.motorConfigWarning : "") + "\n\nSetup will apply:\n• FRAME_CLASS = 1 (Quad)\n• FRAME_TYPE = 1 (X)\n• SERVO1-4_FUNCTION = 33-36\n\nProceed?"
        }
        onAccepted: {
            if (typeof TelemetryProvider !== "undefined" && typeof TelemetryProvider.setupMotorConfig === "function")
                TelemetryProvider.setupMotorConfig()
        }
    }

    Timer {
        id: _motorDialogTimer
        interval: 100; running: false
        onTriggered: {
            if (typeof TelemetryProvider !== "undefined"
                && typeof TelemetryProvider.motorConfigWarning !== "undefined"
                && TelemetryProvider.motorConfigWarning.length > 0)
                motorConfigDialog.open()
        }
    }

    // ── Responsive breakpoint ────────────────────────────────────────────────
    readonly property bool isSingle: width < Config.breakpointSingle

    // ── Root column: step bar + step content ────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Step progress bar ────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Colors.surfaceLight
            border.color: Colors.border; border.width: 0
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Colors.border }

            CE.WizardStepBar {
                id: stepBar
                anchors.fill: parent
                currentStep: _currentStep
            }
        }

        // ── Step content (StackView with slide transitions) ──────────────────
        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Slide left/right animation
            replaceEnter: Transition {
                XAnimator { from: stackView.width * 0.3; to: 0; duration: 200; easing.type: Easing.OutCubic }
                OpacityAnimator { from: 0; to: 1; duration: 200 }
            }
            replaceExit: Transition {
                XAnimator { from: 0; to: -stackView.width * 0.3; duration: 200; easing.type: Easing.OutCubic }
                OpacityAnimator { from: 1; to: 0; duration: 200 }
            }

            // Load Step 0 or skip to Step 1 for Testing mode
            Component.onCompleted: {
                if (FlightSession.isTesting) {
                    _currentStep = 1
                    stackView.replace(step1Component)
                } else {
                    stackView.replace(step0Component)
                }
            }

            // ── Step 0: Operator Selection ────────────────────────────────────
            Component {
                id: step0Component

                CE.WizardStep1Operator {
                    width: stackView.width
                    height: stackView.height

                    onReady: {
                        _currentStep = 1
                        stackView.replace(step1Component)
                    }
                }
            }

            // ── Step 1: System Preflight (70% checklist + 30% weather) ────────
            Component {
                id: step1Component

                Item {
                    width: stackView.width
                    height: stackView.height

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        // ── Top summary bar ──────────────────────────────────
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            color: Colors.surfaceLight
                            border.color: Colors.border; border.width: 0
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Colors.border }

                            RowLayout {
                                anchors.fill: parent; anchors.margins: Config.spacingMedium
                                spacing: Config.spacingMedium

                                Text { text: "🔍 System Checks"; font.pixelSize: Config.fontSizeH3; font.bold: true; color: Colors.textPrimary }
                                Text { text: PreflightManager.passedChecks + "/" + PreflightManager.totalChecks + " passed"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }

                                Item { Layout.fillWidth: true }

                                // Progress indicator
                                Rectangle {
                                    width: 40; height: 40; radius: 20
                                    color: "transparent"
                                    border.color: Colors.accent; border.width: 2
                                    Text {
                                        anchors.centerIn: parent
                                        text: PreflightManager.completionPercent + "%"
                                        font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.accent
                                    }
                                }
                            }
                        }

                        // ── Progress bar ─────────────────────────────────────
                        Rectangle {
                            Layout.fillWidth: true; height: 4; color: Colors.surfaceLight
                            Rectangle {
                                width: parent.width * (PreflightManager.completionPercent / 100)
                                height: parent.height; radius: 0
                                color: _engine.criticalChecks > 0 ? Colors.error : Colors.success
                                Behavior on width { NumberAnimation { duration: Config.animNormal } }
                            }
                        }

                        // ── 70 / 30 split: left panel (swappable) | right sidebar ─────────
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 0

                            // ── LEFT: swappable panel (70%) ───────────────────
                            Item {
                                id: leftPanel
                                Layout.fillHeight: true
                                Layout.preferredWidth: isSingle ? parent.width : parent.width * Config.kChecklistPanelRatio

                                // "checklist" or "gimbal"
                                property string _leftView: "checklist"

                                // ── Checklist view ────────────────────────────
                                Rectangle {
                                    anchors.fill: parent
                                    color: Colors.background
                                    visible: leftPanel._leftView === "checklist"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: Config.spacingSmall
                                        spacing: Config.spacingSmall

                                        // Blocking banner
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: visible ? 40 : 0
                                            visible: _engine.criticalChecks > 0 && !FlightSession.isTesting
                                            color: Colors.errorDim
                                            radius: Config.radiusSmall
                                            border.color: Colors.error; border.width: 1

                                            RowLayout {
                                                anchors.fill: parent; anchors.margins: Config.spacingSmall
                                                Text { text: "\u26a0 Arming blocked — " + _engine.criticalChecks + " critical check(s) must be resolved"; font.pixelSize: Config.fontSizeSmall; color: Colors.error; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                            }
                                        }

                                        // Scrollable check groups
                                        Flickable {
                                            id: checklistFlickable
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            clip: true
                                            contentHeight: grpCol.implicitHeight + Config.spacingMedium
                                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                            boundsBehavior: Flickable.StopAtBounds

                                            ColumnLayout {
                                                id: grpCol
                                                width: checklistFlickable.width
                                                spacing: Config.spacingSmall

                                                Repeater {
                                                    model: _engine.catGroups

                                                    delegate: ColumnLayout {
                                                        required property int index
                                                        required property var modelData
                                                        readonly property bool isOpen: !root._collapsed[index]
                                                        Layout.fillWidth: true; spacing: 0

                                                        Rectangle {
                                                            Layout.fillWidth: true; Layout.preferredHeight: 36
                                                            color: Colors.surfaceLight; radius: Config.radiusSmall
                                                            border.color: Colors.border; border.width: 1

                                                            MouseArea { anchors.fill: parent; onClicked: root.toggleGroup(index); cursorShape: Qt.PointingHandCursor }

                                                            RowLayout {
                                                                anchors.fill: parent; anchors.margins: Config.spacingSmall; spacing: Config.spacingSmall
                                                                Item { Layout.fillWidth: true }
                                                                Text { text: modelData.icon; font.pixelSize: Config.fontSizeBody }
                                                                Text { text: modelData.label; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary }
                                                                Item { Layout.fillWidth: true }
                                                                Text { text: _engine.groupIcon(index); font.pixelSize: Config.fontSizeBody; color: _engine.groupColor(index); font.bold: true }
                                                                Text { text: _engine.groupPassedCount(index); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                                Text { text: isOpen ? "\u25b2" : "\u25bc"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                            }
                                                        }

                                                        Item {
                                                            Layout.fillWidth: true
                                                            Layout.preferredHeight: isOpen ? tileFlow.implicitHeight + Config.spacingSmall : 0
                                                            clip: true
                                                            Behavior on Layout.preferredHeight { NumberAnimation { duration: Config.animNormal; easing.type: Easing.InOutQuad } }

                                                            Flow {
                                                                id: tileFlow
                                                                width: parent.width; spacing: Config.spacingSmall; visible: isOpen

                                                                Repeater {
                                                                    model: PreflightManager.checks

                                                                    delegate: Rectangle {
                                                                        required property var modelData
                                                                        readonly property var chk: modelData
                                                                        readonly property int  cStatus:     chk ? chk.status : 0
                                                                        readonly property bool matchCat:     chk && _engine.catGroups[index] && _engine.catGroups[index].cats.indexOf(chk.checkCategory) >= 0
                                                                        readonly property bool isPassed:     cStatus === 1
                                                                        readonly property bool isFailed:     cStatus === 2
                                                                        readonly property bool isWarning:    cStatus === 3
                                                                        readonly property bool isManual:     (chk ? chk.type : 0) === 1
                                                                        readonly property bool isAction:     (chk ? chk.type : 0) === 2
                                                                        readonly property bool isHighlighted: chk && chk.checkId === root._highlightCheckId
                                                                        readonly property bool isMotorCheck: chk && chk.checkId === "propulsion.motors.spin"

                                                                        visible: matchCat
                                                                        width: isMotorCheck ? parent.width : (isManual || isAction) ? parent.width : (parent.width / 2 - Config.spacingSmall / 2)
                                                                        implicitHeight: isMotorCheck ? (motorLoader.item ? motorLoader.item.implicitHeight : 56) : 56
                                                                        color: "transparent"

                                                                        Loader {
                                                                            id: motorLoader
                                                                            active: isMotorCheck; visible: isMotorCheck; anchors.fill: parent
                                                                            source: "qrc:/qml/cpts/MotorCheckPanel.qml"
                                                                            onLoaded: { if (item) item.checklistCheck = chk }
                                                                        }

                                                                        Rectangle {
                                                                            id: checkTile
                                                                            visible: !isMotorCheck
                                                                            width: parent.width; implicitHeight: 52; radius: Config.radiusSmall
                                                                            color: isHighlighted ? Qt.lighter(Colors.error, 1.8)
                                                                                 : isPassed ? Colors.successDim : isFailed ? Colors.errorDim
                                                                                 : isWarning ? Colors.checkWarnDim : Colors.surface
                                                                            border.color: isHighlighted ? Colors.error
                                                                                        : isPassed ? Colors.success : isFailed ? Colors.error
                                                                                        : isWarning ? Colors.checkWarn : Colors.borderLight
                                                                            border.width: isHighlighted ? 2 : 1
                                                                            Behavior on color { ColorAnimation { duration: Config.animFast } }

                                                                            RowLayout {
                                                                                anchors.fill: parent; anchors.margins: Config.spacingSmall; spacing: Config.spacingSmall
                                                                                Text { text: isPassed ? "✓" : isFailed ? "✗" : isWarning ? "\u26a0" : "\u25cf"; font.pixelSize: Config.fontSizeBody; color: isPassed ? Colors.success : isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.textDisabled }
                                                                                ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: chk.label; font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.textPrimary; elide: Text.ElideRight } Text { visible: chk.message.length > 0; text: chk.message; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; elide: Text.ElideRight } }
                                                                                Button { visible: (isManual || isAction) && !isPassed; text: isManual ? "Confirm" : "Run"; highlighted: true; font.pixelSize: Config.fontSizeSmall; Layout.preferredHeight: 26; Layout.preferredWidth: 54; onClicked: { if (isAction) { var c = PreflightManager.checkById(chk.checkId); if (c) { c.evaluate(); if (c.status === 1) return; c.confirm("Action completed") } } else { chk.confirm("Operator confirmed") } } }
                                                                                Rectangle { visible: isPassed; color: Colors.success; radius: Config.radiusSmall; Layout.preferredHeight: 22; Layout.preferredWidth: 44; Text { anchors.centerIn: parent; text: "PASS"; color: Colors.background; font.pixelSize: 9; font.bold: true } }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }

                                                        Item { Layout.preferredHeight: Config.spacingSmall }
                                                    }
                                                }

                                                Text { visible: PreflightManager.totalChecks === 0; text: qsTr("No checks loaded\nConnect to a vehicle"); font.pixelSize: Config.fontSizeBody; color: Colors.textDisabled; Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true; Layout.preferredHeight: 80 }
                                                Item { Layout.fillHeight: true }
                                            }
                                        }
                                    }
                                }

                                // ── Gimbal Test view (inline, replaces checklist in left 70%) ──
                                Loader {
                                    id: gimbalLoader
                                    anchors.fill: parent
                                    active:  leftPanel._leftView === "gimbal"
                                    visible: leftPanel._leftView === "gimbal"
                                    source: active ? "GimbalTest.qml" : ""
                                }
                            }

                            // ── RIGHT: Weather sidebar (30%, always visible) ──
                            Rectangle {
                                visible: !isSingle
                                Layout.fillHeight: true
                                Layout.preferredWidth: parent.width * 0.30
                                color: Qt.rgba(0,0,0,0.05)
                                border.color: Qt.rgba(1,1,1,0.08); border.width: 1

                                Flickable {
                                    anchors.fill: parent
                                    contentHeight: weatherPanel.implicitHeight
                                    clip: true
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                    CE.WeatherPanel {
                                        id: weatherPanel
                                        width: parent.width
                                        cardBg: Qt.rgba(0,0,0,0.08)
                                    }
                                }
                            }
                        }

                        // ── Footer: adapts based on active left-panel view ────
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Config.kFooterHeight
                            color: Colors.footerBg
                            border.color: Colors.border; border.width: 0
                            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Colors.border }

                            // ── Checklist footer (visible when showing checklist) ──
                            RowLayout {
                                anchors.fill: parent; anchors.margins: Config.spacingMedium
                                spacing: Config.spacingMedium
                                visible: leftPanel._leftView === "checklist"

                                // Gate status indicator
                                Rectangle {
                                    width: 18; height: 18; radius: 9
                                    color: _engine.criticalChecks > 0 ? Colors.error : Colors.success
                                    Text { anchors.centerIn: parent; text: _engine.criticalChecks > 0 ? "✗" : "✓"; font.pixelSize: 11; font.bold: true; color: Colors.background }
                                }
                                Text {
                                    text: _engine.criticalChecks > 0
                                          ? "Blocked — " + _engine.criticalChecks + " blocker(s)"
                                          : _engine.pendingChecks > 0 ? _engine.pendingChecks + " pending"
                                          : PreflightManager.totalChecks > 0 ? "All checks passed"
                                          : "No checks"
                                    font.pixelSize: Config.fontSizeBody; font.bold: true
                                    color: _engine.criticalChecks > 0 ? Colors.error : Colors.success
                                }

                                Item { Layout.fillWidth: true }

                                // ArmGate mode badge
                                Rectangle {
                                    Layout.preferredHeight: 26; Layout.preferredWidth: modeLbl.implicitWidth + Config.spacingMedium * 2
                                    radius: 13; color: Colors.accentDim; border.color: Colors.accent; border.width: 1
                                    Text { id: modeLbl; anchors.centerIn: parent; text: ArmingGate.mode === 0 ? "PASSIVE" : ArmingGate.mode === 1 ? "ACTIVE" : "HYBRID"; font.pixelSize: Config.fontSizeSmall; font.bold: true; color: Colors.accent }
                                }

                                Item { Layout.fillWidth: true }

                                // Warning acknowledgment (shown only when warnings but no blockers)
                                property bool _warnAcknowledged: false
                                Rectangle {
                                    visible: _engine.criticalChecks === 0 && _engine.warnChecks > 0
                                    height: 28; Layout.preferredWidth: 220; radius: Config.radiusSmall
                                    color: parent._warnAcknowledged ? Colors.checkWarnDim : "transparent"
                                    border.color: Colors.checkWarn; border.width: 1

                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: Config.spacingSmall; spacing: 4
                                        Rectangle { width: 14; height: 14; radius: 2; color: parent.parent.parent._warnAcknowledged ? Colors.checkWarn : "transparent"; border.color: Colors.checkWarn; border.width: 1; Text { anchors.centerIn: parent; text: "✓"; font.pixelSize: 9; color: Colors.background; visible: parent.parent.parent.parent._warnAcknowledged } }
                                        Text { text: qsTr("Acknowledge %1 warning(s)").arg(_engine.warnChecks); font.pixelSize: Config.fontSizeSmall; color: Colors.checkWarn }
                                    }

                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.parent._warnAcknowledged = !parent.parent._warnAcknowledged }
                                }

                                // Next → Gimbal Test button
                                Rectangle {
                                    id: _nextBtn
                                    readonly property bool _allowed: FlightSession.isTesting || _engine.criticalChecks === 0
                                    Layout.preferredWidth: 190; height: 32; radius: Config.radiusMedium
                                    color: _allowed ? Colors.accent : Colors.surfaceLight
                                    border.color: _allowed ? Colors.accent : Colors.border; border.width: 1
                                    Behavior on color { ColorAnimation { duration: Config.animNormal } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Next: Gimbal Test  →"
                                        font.pixelSize: Config.fontSizeBody; font.bold: true
                                        color: _nextBtn._allowed ? Colors.background : Colors.textDisabled
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: _nextBtn._allowed ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        enabled: _nextBtn._allowed
                                        onClicked: {
                                            leftPanel._leftView = "gimbal"
                                        }
                                    }
                                }
                            }

                            // ── Gimbal-view footer (visible when showing gimbal test) ──
                            RowLayout {
                                anchors.fill: parent; anchors.margins: Config.spacingMedium
                                spacing: Config.spacingMedium
                                visible: leftPanel._leftView === "gimbal"

                                // Back to checklist
                                Rectangle {
                                    Layout.preferredWidth: 190; height: 32; radius: Config.radiusMedium
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "←  Back to Checklist"
                                        font.pixelSize: Config.fontSizeBody; font.bold: true
                                        color: Colors.textSecondary
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: leftPanel._leftView = "checklist"
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "🎯  Gimbal Test"
                                    font.pixelSize: Config.fontSizeBody; font.bold: true
                                    color: Colors.textPrimary
                                }

                                Item { Layout.fillWidth: true }

                                // Next → Hardware Review (advances to step2Component)
                                Rectangle {
                                    Layout.preferredWidth: 190; height: 32; radius: Config.radiusMedium
                                    color: Colors.accent
                                    border.color: Colors.accent; border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Next: Review  →"
                                        font.pixelSize: Config.fontSizeBody; font.bold: true
                                        color: Colors.background
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            _currentStep = 2
                                            stackView.replace(step2Component)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Step 2: Hardware Verification ─────────────────────────────────
            Component {
                id: step2Component

                CE.WizardStep3Hardware {
                    width: stackView.width
                    height: stackView.height

                    onBackClicked: {
                        _currentStep = 1
                        stackView.replace(step1Component)
                    }

                    onNextClicked: {
                        // Capture hardware results before navigating
                        root._cameraOk = cameraOk
                        root._gimbalOk = gimbalOk
                        _currentStep = 3
                        stackView.replace(step3Component)
                    }
                }
            }

            // ── Step 3: Final Review & Arming ─────────────────────────────────
            Component {
                id: step3Component

                CE.WizardStep4Review {
                    width: stackView.width
                    height: stackView.height
                    cameraOk: root._cameraOk
                    gimbalOk: root._gimbalOk

                    onBackClicked: {
                        _currentStep = 2
                        stackView.replace(step2Component)
                    }
                }
            }
        }
    }

    // ── Disconnect guard overlay ─────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Colors.background; opacity: 0.92
        visible: VehicleTelemetry.disconnectGuardActive
        z: 200

        ColumnLayout {
            anchors.centerIn: parent; spacing: Config.spacingLarge
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72; Layout.preferredHeight: 72; radius: 36
                color: Colors.surface; border.color: Colors.error; border.width: 2
                Text { anchors.centerIn: parent; text: "⚠"; font.pixelSize: 26; color: Colors.error }
            }
            Text { text: "Connection Lost"; font.pixelSize: Config.fontSizeH2; font.bold: true; color: Colors.error; Layout.alignment: Qt.AlignHCenter }
            Text { text: VehicleTelemetry.disconnectGuardMessage; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Checks suspended — reconnecting..."; font.pixelSize: Config.fontSizeBody; color: Colors.textDisabled; Layout.alignment: Qt.AlignHCenter }
        }
    }
}
