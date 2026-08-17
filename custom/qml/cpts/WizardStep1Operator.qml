// Component: WizardStep1Operator
// Purpose: Step 1 of the preflight wizard. Collects operator identity and session mode
//   (Training or Operational) before advancing to system checks.
// Signals:
//   ready() — emitted when all required fields are filled and user clicks "Start Preflight"
// State written:
//   OperatorManager.selectOperator() — sets the active operator
//   FlightSession.startTrainingSession() / startFlightSession() — begins the audit trail

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Item {
    id: root

    signal ready()

    // ── Internal state ──────────────────────────────────────────────────────
    property int  _mode: -1             // 0 = Training, 1 = Operational, 2 = Testing
    property bool _trainingMode: _mode === 0
    property bool _operationalMode: _mode === 1
    property bool _testingMode: _mode === 2

    // Validation: required fields must be filled before proceeding
    readonly property bool _canProceed: {
        if (!OperatorManager.hasCurrentOperator) return false
        if (_mode < 0) return false
        if (_testingMode) return true  // Testing: no form fields needed
        if (_trainingMode) return instructorField.text.trim().length > 0 && traineeField.text.trim().length > 0
        return pilotField.text.trim().length > 0
    }

    // ── Background fetch on first load ──────────────────────────────────────
    Component.onCompleted: {
        OperatorManager.loadOperators()
        if (typeof WeatherProvider !== "undefined" && TelemetryProvider) {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat !== 0 || lon !== 0)
                Qt.callLater(function() { WeatherProvider.fetchWeather(lat, lon) })
        }
    }

    // ── Main layout ─────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingLarge

        // Title
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("✈  Mission Setup")
                font.pixelSize: Config.fontSizeH2
                font.bold: true
                color: Colors.textPrimary
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: qsTr("Identify the operator and select flight mode before beginning checks")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        // ── Two-column form + mode tiles ────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Config.spacingLarge

            // ── LEFT: Operator credentials ──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Colors.surface
                radius: Config.radiusMedium
                border.color: Colors.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingMedium

                    // Operator selector header
                    Text {
                        text: qsTr("👤  Operator")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.accent
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

                    // Operator dropdown
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall

                        Text {
                            text: qsTr("Select Operator")
                            font.pixelSize: Config.fontSizeBody
                            color: Colors.textSecondary
                        }

                        ComboBox {
                            id: operatorCombo
                            Layout.fillWidth: true
                            textRole: "name"
                            valueRole: "id"
                            font.pixelSize: Config.fontSizeBody
                            model: OperatorManager.operators

                            background: Rectangle {
                                color: Colors.surfaceLight
                                border.color: operatorCombo.activeFocus ? Colors.accent : Colors.border
                                border.width: operatorCombo.activeFocus ? 2 : 1
                                radius: Config.radiusSmall
                            }

                            contentItem: Text {
                                leftPadding: Config.spacingSmall
                                text: operatorCombo.currentIndex >= 0
                                      ? operatorCombo.currentText
                                      : qsTr("Select operator...")
                                color: operatorCombo.currentIndex >= 0
                                       ? Colors.textPrimary
                                       : Colors.textDisabled
                                font.pixelSize: Config.fontSizeBody
                                verticalAlignment: Text.AlignVCenter
                            }

                            indicator: Text {
                                x: operatorCombo.width - width - Config.spacingSmall
                                y: (operatorCombo.height - height) / 2
                                text: "▼"
                                color: Colors.textSecondary
                                font.pixelSize: 10
                            }

                            onActivated: {
                                var id = operatorCombo.currentValue
                                if (id > 0) OperatorManager.selectOperator(id)
                            }
                        }

                        // Add new operator row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Config.spacingSmall

                            TextField {
                                id: newOpField
                                Layout.fillWidth: true
                                placeholderText: qsTr("New operator name")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textPrimary
                                background: Rectangle {
                                    color: Colors.surfaceLight
                                    border.color: newOpField.activeFocus ? Colors.accent : Colors.border
                                    border.width: 1
                                    radius: Config.radiusSmall
                                }
                            }

                            Rectangle {
                                width: 52; height: 32
                                radius: Config.radiusSmall
                                color: newOpField.text.trim().length > 0 ? Colors.accent : Colors.surfaceLight
                                border.color: Colors.border
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Add")
                                    font.pixelSize: Config.fontSizeBody
                                    font.bold: true
                                    color: newOpField.text.trim().length > 0 ? Colors.background : Colors.textDisabled
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: newOpField.text.trim().length > 0
                                    onClicked: {
                                        var id = OperatorManager.addOperator(newOpField.text.trim(), "Pilot")
                                        if (id > 0) {
                                            OperatorManager.selectOperator(id)
                                            newOpField.text = ""
                                            var idx = operatorCombo.indexOfValue(id)
                                            if (idx >= 0) operatorCombo.currentIndex = idx
                                        }
                                    }
                                }
                            }
                        }

                        // Warning: no operator selected
                        Rectangle {
                            Layout.fillWidth: true
                            height: 28
                            visible: !OperatorManager.hasCurrentOperator
                            color: Colors.warningDim
                            radius: Config.radiusSmall

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("⚠  Select or add an operator before continuing")
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.warning
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

                    // ── Mode-specific fields ─────────────────────────────────
                    Text {
                        text: qsTr("Flight Details")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: Colors.accent
                        visible: _mode >= 0
                    }

                    // Training mode fields
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall
                        visible: _trainingMode

                        LabelField {
                            id: instructorField
                            label: qsTr("Instructor Name *")
                            placeholder: qsTr("Required")
                        }

                        LabelField {
                            id: traineeField
                            label: qsTr("Trainee Name *")
                            placeholder: qsTr("Required")
                        }

                        LabelField {
                            id: rcInstructorField
                            label: qsTr("Instructor RC Link")
                            placeholder: qsTr("e.g. FrSky TX16S")
                        }

                        LabelField {
                            id: rcTraineeField
                            label: qsTr("Trainee RC Link")
                            placeholder: qsTr("e.g. RadioMaster TX12")
                        }
                    }

                    // Operational mode fields
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Config.spacingSmall
                        visible: _operationalMode

                        LabelField {
                            id: pilotField
                            label: qsTr("Pilot Name *")
                            placeholder: qsTr("Required")
                        }

                        LabelField {
                            id: observerField
                            label: qsTr("Observer Name")
                            placeholder: qsTr("Optional")
                        }

                        LabelField {
                            id: rcPilotField
                            label: qsTr("RC Link ID")
                            placeholder: qsTr("e.g. FrSky TX16S")
                        }
                    }

                    // Placeholder when no mode selected yet
                    Text {
                        text: qsTr("← Select a flight mode to fill in details")
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textDisabled
                        visible: _mode < 0
                        Layout.alignment: Qt.AlignHCenter
                    }

                    // Testing mode info
                    Rectangle {
                        Layout.fillWidth: true
                        height: testModeCol.implicitHeight + Config.spacingMedium * 2
                        visible: _testingMode
                        color: Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.08)
                        radius: Config.radiusSmall
                        border.color: Colors.accentCyan
                        border.width: 1

                        ColumnLayout {
                            id: testModeCol
                            anchors.centerIn: parent
                            spacing: Config.spacingSmall

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("⚙️  Testing Mode")
                                font.pixelSize: Config.fontSizeH3
                                font.bold: true
                                color: Colors.accentCyan
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("No audit logs or flight records will be created.\nArming is permitted without full checks.")
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ── RIGHT: Mode tiles ────────────────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: parent.width * 0.38
                Layout.fillHeight: true
                spacing: Config.spacingMedium

                Text {
                    text: qsTr("Flight Mode")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                // Training tile
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    radius: Config.radiusLarge
                    color: _mode === 0 ? Colors.accentDim : Colors.surface
                    border.color: _mode === 0 ? Colors.accent : Colors.border
                    border.width: _mode === 0 ? 2 : 1
                    opacity: OperatorManager.hasCurrentOperator ? 1.0 : 0.5

                    Behavior on color       { ColorAnimation { duration: Config.animNormal } }
                    Behavior on border.color{ ColorAnimation { duration: Config.animNormal } }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Config.spacingSmall

                        Text { Layout.alignment: Qt.AlignHCenter; text: "🎓"; font.pixelSize: 34 }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Training Flight")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Arming disabled • Full audit")
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                        }
                    }

                    Rectangle {
                        visible: _mode === 0
                        anchors.top: parent.top; anchors.right: parent.right
                        anchors.margins: Config.spacingSmall
                        width: 20; height: 20; radius: 10
                        color: Colors.accent
                        Text { anchors.centerIn: parent; text: "✓"; color: Colors.background; font.pixelSize: 11; font.bold: true }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: OperatorManager.hasCurrentOperator
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root._mode = 0 }
                    }
                }

                // Operational tile
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    radius: Config.radiusLarge
                    color: _mode === 1 ? Qt.rgba(0.1, 0.7, 0.3, 0.15) : Colors.surface
                    border.color: _mode === 1 ? Colors.success : Colors.border
                    border.width: _mode === 1 ? 2 : 1
                    opacity: OperatorManager.hasCurrentOperator ? 1.0 : 0.5

                    Behavior on color       { ColorAnimation { duration: Config.animNormal } }
                    Behavior on border.color{ ColorAnimation { duration: Config.animNormal } }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Config.spacingSmall

                        Text { Layout.alignment: Qt.AlignHCenter; text: "✈️"; font.pixelSize: 34 }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Operational Flight")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Full audit trail")
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                        }
                    }

                    Rectangle {
                        visible: _mode === 1
                        anchors.top: parent.top; anchors.right: parent.right
                        anchors.margins: Config.spacingSmall
                        width: 20; height: 20; radius: 10
                        color: Colors.success
                        Text { anchors.centerIn: parent; text: "✓"; color: Colors.background; font.pixelSize: 11; font.bold: true }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: OperatorManager.hasCurrentOperator
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root._mode = 1 }
                    }
                }

                // Testing tile
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    radius: Config.radiusLarge
                    color: _mode === 2 ? Qt.rgba(0.0, 0.7, 0.8, 0.15) : Colors.surface
                    border.color: _mode === 2 ? Colors.accentCyan : Colors.border
                    border.width: _mode === 2 ? 2 : 1
                    opacity: OperatorManager.hasCurrentOperator ? 1.0 : 0.5

                    Behavior on color       { ColorAnimation { duration: Config.animNormal } }
                    Behavior on border.color{ ColorAnimation { duration: Config.animNormal } }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Config.spacingSmall

                        Text { Layout.alignment: Qt.AlignHCenter; text: "⚙️"; font.pixelSize: 34 }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Testing")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("No audit • Arm allowed")
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                        }
                    }

                    Rectangle {
                        visible: _mode === 2
                        anchors.top: parent.top; anchors.right: parent.right
                        anchors.margins: Config.spacingSmall
                        width: 20; height: 20; radius: 10
                        color: Colors.accentCyan
                        Text { anchors.centerIn: parent; text: "✓"; color: Colors.background; font.pixelSize: 11; font.bold: true }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: OperatorManager.hasCurrentOperator
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root._mode = 2 }
                    }
                }

                // Vehicle info card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: Colors.surface
                    radius: Config.radiusSmall
                    border.color: Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingSmall
                        spacing: Config.spacingSmall

                        Text { text: "🚁"; font.pixelSize: 20 }
                        ColumnLayout {
                            spacing: 1
                            Text {
                                text: VehicleTelemetry.vehicleType.length > 0
                                      ? VehicleTelemetry.vehicleType
                                      : qsTr("Vehicle")
                                font.pixelSize: Config.fontSizeBody
                                font.bold: true
                                color: Colors.textPrimary
                            }
                            Text {
                                text: TelemetryProvider.isConnected
                                      ? qsTr("● Connected")
                                      : qsTr("○ Disconnected")
                                font.pixelSize: Config.fontSizeSmall
                                color: TelemetryProvider.isConnected ? Colors.success : Colors.error
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // ── Proceed button ───────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    height: 44
                    radius: Config.radiusMedium
                    color: _canProceed ? Colors.accent : Colors.surfaceLight
                    border.color: _canProceed ? Colors.accent : Colors.border
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Config.animNormal } }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Start Preflight  →")
                        font.pixelSize: Config.fontSizeH3
                        font.bold: true
                        color: _canProceed ? Colors.background : Colors.textDisabled
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: _canProceed ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: _canProceed
                        onClicked: {
                            // Log session start to audit trail
                            if (_testingMode) {
                                FlightSession.startTestingSession()
                            } else if (_trainingMode) {
                                FlightSession.startTrainingSession()
                                // Record training details in database
                                var details = {
                                    mode: "training",
                                    instructor: instructorField.text.trim(),
                                    trainee: traineeField.text.trim(),
                                    rcInstructor: rcInstructorField.text.trim(),
                                    rcTrainee: rcTraineeField.text.trim(),
                                    operatorId: OperatorManager.currentOperatorId,
                                    operatorName: OperatorManager.currentOperatorName,
                                    timestamp: new Date().toISOString()
                                }
                                if (FlightSession.currentFlightId > 0) {
                                    Database.insertTelemetryEvent(
                                        FlightSession.currentFlightId,
                                        "TRAINING_SESSION_START",
                                        JSON.stringify(details),
                                        0.0, 0.0, 0, ""
                                    )
                                }
                            } else {
                                // Operational mode: store pilot data and start session
                                var opDetails = {
                                    mode: "operational",
                                    pilot: pilotField.text.trim(),
                                    observer: observerField.text.trim(),
                                    rcLink: rcPilotField.text.trim(),
                                    operatorId: OperatorManager.currentOperatorId,
                                    operatorName: OperatorManager.currentOperatorName,
                                    timestamp: new Date().toISOString()
                                }
                                if (FlightSession.currentFlightId > 0) {
                                    Database.insertTelemetryEvent(
                                        FlightSession.currentFlightId,
                                        "PREFLIGHT_SESSION_START",
                                        JSON.stringify(opDetails),
                                        0.0, 0.0, 0, ""
                                    )
                                }
                            }
                            root.ready()
                        }
                    }
                }
            }
        }
    }

    // ── Inline reusable labeled text field component ─────────────────────────
    component LabelField: ColumnLayout {
        property alias label: _label.text
        property alias placeholder: _field.placeholderText
        property alias text: _field.text

        spacing: 2
        Layout.fillWidth: true

        Text { id: _label; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
        TextField {
            id: _field
            Layout.fillWidth: true
            font.pixelSize: Config.fontSizeBody
            color: Colors.textPrimary
            background: Rectangle {
                color: Colors.surfaceLight
                border.color: _field.activeFocus ? Colors.accent : Colors.border
                border.width: 1
                radius: Config.radiusSmall
            }
        }
    }
}
