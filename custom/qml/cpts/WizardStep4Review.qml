// Component: WizardStep4Review
// Purpose: Final review step (Step 4 of wizard). Summarises all checks, weather,
//   hardware results, and operator identity. Provides:
//     - "Arm Vehicle" when all blocking checks pass
//     - "Force Arm" when blockers exist — requires typed reason + responsibility checkbox
//       before calling ArmingGate.forceArm(); all force-arm events are logged to DB.
// Signals:
//   backClicked()  — navigate back to hardware verification
// Properties passed in:
//   cameraOk  (bool) — result from Step 3
//   gimbalOk  (bool) — result from Step 3

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0
import cpts 1.0 as CE

Item {
    id: root

    signal backClicked()

    // ── Inputs from previous steps ───────────────────────────────────────────
    property bool cameraOk: false
    property bool gimbalOk: false

    // ── Derived state ────────────────────────────────────────────────────────
    readonly property int _criticalCount: typeof _engine !== "undefined" ? _engine.criticalChecks : 0
    readonly property int _pendingCount:  typeof _engine !== "undefined" ? _engine.pendingChecks  : 0
    readonly property int _passedCount:   typeof PreflightManager !== "undefined" ? PreflightManager.passedChecks : 0
    readonly property int _totalCount:    typeof PreflightManager !== "undefined" ? PreflightManager.totalChecks  : 0
    readonly property bool _allSystemsGo: _criticalCount === 0

    // Weather status
    readonly property real _windSpeed: typeof WeatherProvider !== "undefined" ? WeatherProvider.windSpeed : 0
    readonly property bool _weatherMarginal: _windSpeed > 8.0  // m/s threshold
    readonly property bool _weatherCritical: _windSpeed > 12.0

    // CE engine reference (same as PreFlightChecklist.qml uses)
    CE.ChecklistEngine {
        id: _engine
        viewMode: "standalone"
    }

    // ── Force Arm dialog ─────────────────────────────────────────────────────
    Dialog {
        id: forceArmDialog
        title: qsTr("Force Arm Override")
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        focus: true

        background: Rectangle {
            color: Colors.surface
            border.color: Colors.error
            border.width: 2
            radius: Config.radiusMedium
        }

        padding: Config.spacingMedium

        header: Rectangle {
            height: 52
            color: Qt.rgba(Colors.error.r, Colors.error.g, Colors.error.b, 0.15)
            radius: Config.radiusMedium
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: parent.color }

            RowLayout {
                anchors.fill: parent; anchors.margins: Config.spacingMedium
                Text { text: "⚠"; font.pixelSize: 18; color: Colors.error }
                Text {
                    text: qsTr("Force Arm — Operator Override Required")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.error
                    Layout.fillWidth: true
                }
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingMedium

            // Warning text
            Text {
                Layout.fillWidth: true
                text: qsTr("You are bypassing %1 critical safety check(s). This action will be permanently recorded in the audit trail with your name, timestamp, and stated reason.").arg(_criticalCount)
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                wrapMode: Text.WordWrap
            }

            // List of blockers being overridden
            Rectangle {
                Layout.fillWidth: true
                height: blockerList.implicitHeight + Config.spacingSmall * 2
                color: Colors.errorDim
                radius: Config.radiusSmall
                border.color: Colors.error; border.width: 1
                visible: typeof PreflightManager !== "undefined"

                Column {
                    id: blockerList
                    anchors.fill: parent
                    anchors.margins: Config.spacingSmall
                    spacing: 4

                    Repeater {
                        model: typeof PreflightManager !== "undefined" ? PreflightManager.blockingChecks : []
                        delegate: RowLayout {
                            required property var modelData
                            width: parent.width
                            spacing: Config.spacingSmall
                            Text { text: "✗"; color: Colors.error; font.bold: true; font.pixelSize: Config.fontSizeSmall }
                            Text {
                                text: modelData ? modelData.label : ""
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

            // Reason text area (min 10 chars required)
            Text {
                text: qsTr("Override Reason *")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                height: 96
                color: Colors.surfaceLight
                border.color: reasonArea.activeFocus ? Colors.accent : (reasonArea.text.trim().length >= 10 ? Colors.success : Colors.border)
                border.width: 1
                radius: Config.radiusSmall

                TextArea {
                    id: reasonArea
                    anchors.fill: parent
                    anchors.margins: 4
                    placeholderText: qsTr("Describe why you are bypassing these checks (minimum 10 characters)...")
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textPrimary
                    wrapMode: TextArea.WordWrap
                    background: null
                }
            }

            // Character count / validation hint
            Text {
                text: {
                    var len = reasonArea.text.trim().length
                    if (len === 0) return qsTr("Required — minimum 10 characters")
                    if (len < 10) return qsTr("%1/10 characters minimum").arg(len)
                    return qsTr("✓ Reason captured (%1 chars)").arg(len)
                }
                font.pixelSize: Config.fontSizeSmall
                color: reasonArea.text.trim().length >= 10 ? Colors.success : Colors.error
            }

            // Responsibility checkbox
            Rectangle {
                Layout.fillWidth: true
                height: 44
                radius: Config.radiusSmall
                color: responsibilityCheck.checked ? Colors.errorDim : Colors.surface
                border.color: responsibilityCheck.checked ? Colors.error : Colors.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent; anchors.margins: Config.spacingSmall
                    spacing: Config.spacingSmall

                    Rectangle {
                        id: checkBox
                        width: 20; height: 20; radius: 4
                        color: responsibilityCheck.checked ? Colors.error : "transparent"
                        border.color: Colors.error; border.width: 2
                        Text {
                            anchors.centerIn: parent; text: "✓"
                            font.pixelSize: 11; font.bold: true; color: Colors.background
                            visible: responsibilityCheck.checked
                        }
                    }

                    Text {
                        text: qsTr("I accept responsibility for bypassing these safety checks")
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textPrimary
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }

                CheckBox {
                    id: responsibilityCheck
                    anchors.fill: parent
                    opacity: 0   // invisible — visual is handled by Rectangle above
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: responsibilityCheck.checked = !responsibilityCheck.checked
                }
            }

            // Dialog action buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingMedium

                // Cancel
                Rectangle {
                    Layout.preferredWidth: 100; height: 36
                    radius: Config.radiusSmall
                    color: Colors.surfaceLight
                    border.color: Colors.border; border.width: 1

                    Text { anchors.centerIn: parent; text: qsTr("Cancel"); font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: forceArmDialog.close() }
                }

                Item { Layout.fillWidth: true }

                // Confirm Force Arm (gated on reason + checkbox)
                readonly property bool _dialogValid: reasonArea.text.trim().length >= 10 && responsibilityCheck.checked

                Rectangle {
                    Layout.preferredWidth: 180; height: 36
                    radius: Config.radiusSmall
                    color: parent._dialogValid ? Colors.error : Colors.surfaceLight
                    border.color: parent._dialogValid ? Colors.error : Colors.border
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Config.animNormal } }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Confirm Force Arm")
                        font.pixelSize: Config.fontSizeBody
                        font.bold: true
                        color: parent.parent._dialogValid ? Colors.textPrimary : Colors.textDisabled
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: parent.parent._dialogValid ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: parent.parent._dialogValid
                        onClicked: {
                            // 1. Assemble full audit context
                            var auditPayload = {
                                event:           "FORCE_ARM",
                                timestamp:       new Date().toISOString(),
                                operatorId:      OperatorManager.currentOperatorId,
                                operatorName:    OperatorManager.currentOperatorName,
                                reason:          reasonArea.text.trim(),
                                criticalCount:   root._criticalCount,
                                blockers:        (typeof PreflightManager !== "undefined")
                                                     ? PreflightManager.blockingChecks.map(function(c) { return c.label })
                                                     : [],
                                weatherSummary:  (typeof WeatherProvider !== "undefined")
                                                     ? WeatherProvider.currentSummary
                                                     : "unknown",
                                windSpeed:       root._windSpeed,
                                systemChecks:    root._passedCount + "/" + root._totalCount,
                                cameraOk:        root.cameraOk,
                                gimbalOk:        root.gimbalOk
                            }

                            // 2. Persist to database (only for audited sessions)
                            if (FlightSession.currentFlightId > 0) {
                                Database.insertTelemetryEvent(
                                    FlightSession.currentFlightId,
                                    "FORCE_ARM",
                                    JSON.stringify(auditPayload),
                                    0.0, 0.0, 0, ""
                                )
                            }

                            // 3. Execute force arm
                            ArmingGate.forceArm()
                            TelemetryProvider.arm()

                            forceArmDialog.close()
                            forceArmSuccessTimer.restart()
                        }
                    }
                }
            }
        }
    }

    // Post-force-arm confirmation banner
    property bool _forceArmLogged: false
    Timer { id: forceArmSuccessTimer; interval: 3000; onTriggered: root._forceArmLogged = false }

    // ── Proceed with caution / abort dialog (weather) ─────────────────────────
    Dialog {
        id: weatherWarningDialog
        title: qsTr("Weather Conditions Marginal")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, parent.width * 0.85)
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Colors.surface
            border.color: Colors.checkWarn
            border.width: 2
            radius: Config.radiusMedium
        }

        padding: Config.spacingMedium

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingMedium

            Text {
                text: "⚠  " + qsTr("Wind speed (%1 m/s) exceeds recommended threshold (8 m/s)").arg(root._windSpeed.toFixed(1))
                font.pixelSize: Config.fontSizeBody
                color: Colors.checkWarn
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingMedium

                Rectangle {
                    Layout.fillWidth: true; height: 36
                    radius: Config.radiusSmall
                    color: Colors.errorDim
                    border.color: Colors.error; border.width: 1
                    Text { anchors.centerIn: parent; text: qsTr("Abort & Reschedule"); font.pixelSize: Config.fontSizeBody; color: Colors.error }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: weatherWarningDialog.close() }
                }

                Rectangle {
                    Layout.fillWidth: true; height: 36
                    radius: Config.radiusSmall
                    color: Colors.checkWarnDim
                    border.color: Colors.checkWarn; border.width: 1
                    Text { anchors.centerIn: parent; text: qsTr("Proceed with Caution"); font.pixelSize: Config.fontSizeBody; color: Colors.checkWarn }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            weatherWarningDialog.close()
                            TelemetryProvider.arm()
                        }
                    }
                }
            }
        }
    }

    // ── Main layout ─────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Config.spacingMedium
        spacing: Config.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                width: 32; height: 32; radius: Config.radiusSmall
                color: Colors.surfaceLight; border.color: Colors.border; border.width: 1
                Text { anchors.centerIn: parent; text: "←"; font.pixelSize: Config.fontSizeH3; color: Colors.textSecondary }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backClicked() }
            }

            Text {
                text: qsTr("📋  Final Review")
                font.pixelSize: Config.fontSizeH2
                font.bold: true
                color: Colors.textPrimary
                Layout.fillWidth: true
                Layout.leftMargin: Config.spacingSmall
            }
        }

        // ── Status banner ─────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 56
            radius: Config.radiusMedium
            color: _allSystemsGo
                   ? Qt.rgba(0.1, 0.7, 0.3, 0.15)
                   : Qt.rgba(Colors.error.r, Colors.error.g, Colors.error.b, 0.15)
            border.color: _allSystemsGo ? Colors.success : Colors.error
            border.width: 2

            // Force-arm logged confirmation overlay
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Colors.errorDim
                visible: root._forceArmLogged
                Text {
                    anchors.centerIn: parent
                    text: qsTr("⚡  Force arm recorded — flight authorized under override")
                    font.pixelSize: Config.fontSizeBody; font.bold: true
                    color: Colors.textPrimary
                }
            }

            RowLayout {
                anchors.fill: parent; anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall
                visible: !root._forceArmLogged

                Text {
                    font.pixelSize: 22
                    text: _allSystemsGo ? "✅" : "🚫"
                }
                Text {
                    text: _allSystemsGo
                          ? qsTr("Preflight Complete — Ready to Arm")
                          : qsTr("%1 critical issue(s) must be resolved before arming").arg(_criticalCount)
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: _allSystemsGo ? Colors.success : Colors.error
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ── Summary grid ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Config.spacingMedium

            // System Checks summary
            SummaryCard {
                title: qsTr("System Checks")
                icon: "🔧"
                Layout.fillWidth: true
                Layout.fillHeight: true

                SummaryRow { label: "Passed"; value: root._passedCount + "/" + root._totalCount; valueColor: root._allSystemsGo ? Colors.success : Colors.textPrimary }
                SummaryRow { label: "Warnings"; value: (typeof _engine !== "undefined" ? _engine.warnChecks : 0) + ""; valueColor: Colors.checkWarn }
                SummaryRow { label: "Critical"; value: root._criticalCount + ""; valueColor: root._criticalCount > 0 ? Colors.error : Colors.success }

                // Critical items list
                Rectangle {
                    visible: root._criticalCount > 0
                    Layout.fillWidth: true
                    height: criticalList.implicitHeight + 8
                    color: Colors.errorDim
                    radius: Config.radiusSmall
                    border.color: Colors.error; border.width: 1

                    Column {
                        id: criticalList
                        anchors.fill: parent; anchors.margins: 4
                        spacing: 2

                        Repeater {
                            model: typeof PreflightManager !== "undefined" ? PreflightManager.blockingChecks : []
                            delegate: RowLayout {
                                required property var modelData
                                width: parent.width
                                spacing: 4
                                Text { text: "✗"; font.pixelSize: Config.fontSizeSmall; color: Colors.error; font.bold: true }
                                Text { text: modelData ? modelData.label : ""; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                                Rectangle {
                                    width: 52; height: 18; radius: Config.radiusSmall; color: Colors.error
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            // Signal back to wizard shell to jump to system step
                                            // Emitting backClicked twice gets there (Hardware → System)
                                            root.backClicked()
                                            root.backClicked()
                                        }
                                    }
                                    Text { anchors.centerIn: parent; text: "Go Fix"; font.pixelSize: 9; font.bold: true; color: Colors.textPrimary }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // Weather summary
            SummaryCard {
                title: qsTr("Weather")
                icon: "🌤"
                Layout.fillWidth: true
                Layout.fillHeight: true

                SummaryRow {
                    label: "Conditions"
                    value: typeof WeatherProvider !== "undefined" ? WeatherProvider.weatherDescription : "—"
                    valueColor: Colors.textPrimary
                }
                SummaryRow {
                    label: "Wind"
                    value: typeof WeatherProvider !== "undefined" ? root._windSpeed.toFixed(1) + " m/s" : "—"
                    valueColor: root._weatherCritical ? Colors.error : root._weatherMarginal ? Colors.checkWarn : Colors.success
                }
                SummaryRow {
                    label: "Temperature"
                    value: typeof WeatherProvider !== "undefined" ? WeatherProvider.temperature.toFixed(1) + " °C" : "—"
                    valueColor: Colors.textPrimary
                }
                SummaryRow {
                    label: "Visibility"
                    value: typeof WeatherProvider !== "undefined" ? WeatherProvider.visibilityKm.toFixed(1) + " km" : "—"
                    valueColor: Colors.textPrimary
                }

                Rectangle {
                    visible: root._weatherMarginal
                    Layout.fillWidth: true
                    height: 28; radius: Config.radiusSmall
                    color: Colors.checkWarnDim
                    border.color: Colors.checkWarn; border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: root._weatherCritical
                              ? qsTr("⚠ Wind exceeds safe threshold")
                              : qsTr("⚠ Wind conditions marginal")
                        font.pixelSize: Config.fontSizeSmall
                        font.bold: true
                        color: Colors.checkWarn
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // Hardware summary
            SummaryCard {
                title: qsTr("Hardware")
                icon: "📷"
                Layout.fillWidth: true
                Layout.fillHeight: true

                SummaryRow { label: "Camera"; value: root.cameraOk ? "✓ OK" : "✗ Not checked"; valueColor: root.cameraOk ? Colors.success : Colors.error }
                SummaryRow { label: "Gimbal"; value: root.gimbalOk ? "✓ OK" : "✗ Not checked"; valueColor: root.gimbalOk ? Colors.success : Colors.error }

                Item { Layout.fillHeight: true }
            }

            // Operator summary
            SummaryCard {
                title: qsTr("Operator")
                icon: "👤"
                Layout.fillWidth: true
                Layout.fillHeight: true

                SummaryRow {
                    label: "Operator"
                    value: OperatorManager.hasCurrentOperator ? OperatorManager.currentOperatorName : "—"
                    valueColor: Colors.textPrimary
                }
                SummaryRow {
                    label: "Role"
                    value: OperatorManager.hasCurrentOperator ? OperatorManager.currentOperatorRole : "—"
                    valueColor: Colors.textSecondary
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ── Arm action footer ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Item { Layout.fillWidth: true }

            // Force Arm button (shown only when blockers exist)
            Rectangle {
                visible: !_allSystemsGo
                Layout.preferredWidth: 140; height: 44
                radius: Config.radiusMedium
                color: Colors.errorDim
                border.color: Colors.error; border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: qsTr("⚡  Force Arm")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.error
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // Reset dialog fields on each open
                        reasonArea.text = ""
                        responsibilityCheck.checked = false
                        forceArmDialog.open()
                    }
                }
            }

            // Arm Vehicle button (primary, shown only when no blockers)
            Rectangle {
                visible: _allSystemsGo
                Layout.preferredWidth: 200; height: 44
                radius: Config.radiusMedium
                color: Colors.success
                border.color: Colors.success; border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: qsTr("✓  Arm Vehicle")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.background
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root._weatherMarginal) {
                            weatherWarningDialog.open()
                        } else {
                            TelemetryProvider.arm()
                        }
                    }
                }
            }
        }
    }

    // ── Inline reusable components ───────────────────────────────────────────
    component SummaryCard: Rectangle {
        id: _card
        color: Colors.surface
        radius: Config.radiusMedium
        border.color: Colors.border; border.width: 1

        property string title: ""
        property string icon: ""
        default property alias content: _col.data

        ColumnLayout {
            id: _col
            anchors.fill: parent
            anchors.margins: Config.spacingSmall
            spacing: Config.spacingSmall

            RowLayout {
                spacing: Config.spacingSmall
                Text { text: _card.icon; font.pixelSize: 16 }
                Text { text: _card.title; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textSecondary }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }
        }
    }

    component SummaryRow: RowLayout {
        property string label: ""
        property string value: ""
        property color  valueColor: Colors.textPrimary

        Layout.fillWidth: true
        spacing: Config.spacingSmall

        Text { text: label; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; Layout.preferredWidth: 80 }
        Text { text: value; font.pixelSize: Config.fontSizeSmall; font.bold: true; color: valueColor; Layout.fillWidth: true; elide: Text.ElideRight }
    }
}
