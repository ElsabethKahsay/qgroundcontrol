// Component: PreflightChecklistView
// Purpose: Main checklist view for the Analyze page. Displays all preflight checks grouped by
//   category in a responsive card grid with multicolor progress bar and blocking-issue banner.
// Properties:
//   catModels (var) — readonly mapping of category ID to ListModel
//   catInfo (var) — readonly mapping of category ID to {label, icon}
//   catOrder (var) — readonly ordered list of category IDs for display
//   typeNames (var) — readonly list of check type labels ["Auto", "Manual", "Action"]
//   _runningChecks (var) — internal tracking of which action checks are running
//   _expandedChecks (var) — internal tracking of expanded check card IDs
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0
import cpts 1.0 as CE

Rectangle {
    id: root

    CE.ChecklistEngine {
        id: _engine
        viewMode: "analyze"
    }
    anchors.fill: parent
    color: Colors.background

    readonly property var catModels: ({
        0: CatModel0, 1: CatModel1, 2: CatModel2, 3: CatModel3,
        4: CatModel4, 5: CatModel5, 6: CatModel6, 7: CatModel7
    })

    readonly property int _blockers: typeof PreflightChecklistModel !== "undefined" ? PreflightChecklistModel.blockingFailedCount : 0
    readonly property int _pend: typeof PreflightManager !== "undefined" ? PreflightManager.pendingChecks : 0
    readonly property int _pct: typeof PreflightChecklistModel !== "undefined" ? PreflightChecklistModel.completionPercent : 0
    readonly property int _total: typeof PreflightManager !== "undefined" ? PreflightManager.totalChecks : 0
    readonly property int _passed: typeof PreflightManager !== "undefined" ? PreflightManager.passedChecks : 0
    readonly property int _issueCount: typeof PreflightManager !== "undefined" ? PreflightManager.blockingCount : 0

    property var _runningChecks: ({})

    // The shared pack dialog (ChecklistEngine.BatteryPackDialog),
    // opened by the "Battery pack" item in the Power section.
    CE.ChecklistEngine.BatteryPackDialog {
        id: batteryPackDialog
        parent: Overlay.overlay
    }
    // Opened by the "Payload" item (Airframe & Physical).
    CE.ChecklistEngine.PayloadDialog {
        id: payloadDialog
        parent: Overlay.overlay
    }
    // Opened by the "Motor table" item (Power section, copters).
    CE.ChecklistEngine.MotorTableDialog {
        id: motorTableDialog
        parent: Overlay.overlay
    }
    // Opened by the "Mission estimate" item (Power section, copters).
    CE.ChecklistEngine.MissionEstimateDialog {
        id: missionEstimateDialog
        objectName: "missionEstimateDialog"
        parent: Overlay.overlay
    }
    property var _expandedChecks: ({})

    // ── Vehicle-type aware "Next → Gimbal Test" flow ───────────────────────
    // The Gimbal test page is hosted inline (see gimbalOverlay at the bottom of
    // this file) rather than pushed on a page stack: Config.navigatePush() is a
    // no-op in this build because the root window exposes no mainStackView.
    property string _leftView: "checklist"   // "checklist" | "gimbal"

    // Mirrors GimbalTest.qml: multirotors carry no servos / control surfaces.
    readonly property bool _isMultirotor: {
        var vt = (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager.vehicleType : ""
        return vt === "QUAD" || vt === "HEX" || vt === "OCTA" || vt === "TRI"
    }

    // Explicit airframe enumeration for the Next button. Every profile-manager
    // type plus the unresolved state keeps it, so a real vehicle can never lose
    // the button. GimbalTest hides its own Control Surface sweep for
    // multirotors; the Restricted Zones airspace panel stays for all vehicles.
    readonly property bool _showNextBtn: {
        var vt = (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager.vehicleType : "UNKNOWN"
        return vt === "" || vt === "UNKNOWN"
                || vt === "FIXED_WING" || vt === "VTOL" || vt === "ROVER"
                || vt === "QUAD" || vt === "HEX" || vt === "OCTA" || vt === "TRI" || vt === "HELI"
    }

    function nextBlocker(index) {
        if (typeof PreflightManager === "undefined") return ""
        var b = PreflightManager.blockingChecks
        if (!b || b.length <= index) return ""
        var item = b[index]
        return item && item.label ? item.label : ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Combined summary bar (checks passed + blocking issues) ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            Layout.topMargin: Config.spacingSmall
            radius: 10
            visible: _total > 0

            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Colors.surface }
                GradientStop { position: 0.5; color: Colors.surfaceLight }
                GradientStop { position: 1.0; color: Colors.surfaceLight }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 12
                spacing: 16

                // Pass count
                ColumnLayout {
                    spacing: 1
                    Text {
                        text: _passed + "/" + _total + " checks passed"
                        font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary
                    }
                    Text {
                        text: _blockers > 0
                              ? "\u26A0 Arming blocked: " + _blockers + " critical " + (_blockers === 1 ? "item must" : "items must") + " be fixed"
                              : _issueCount > 0 ? _issueCount + " critical issue" + (_issueCount !== 1 ? "s" : "") : "No critical issues"
                        font.pixelSize: Config.fontSizeSmall
                        color: _issueCount > 0 ? Colors.statusFail : Colors.statusPass
                    }
                }

                // First / next fix hint (fills the middle)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: nextBlocker(0).length > 0 ? "First: Fix " + nextBlocker(0) : ""
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.statusFail
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        visible: nextBlocker(0).length > 0
                    }
                    Text {
                        text: nextBlocker(1).length > 0 ? "Next: Fix " + nextBlocker(1) : ""
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        visible: nextBlocker(1).length > 0
                    }
                }

                // Circular progress indicator
                Rectangle {
                    width: 36; height: 36; radius: 18
                    color: "transparent"
                    border.color: Colors.accent; border.width: 2

                    Text {
                        anchors.centerIn: parent
                        text: _pct + "%"
                        font.pixelSize: 11; font.bold: true; color: Colors.accent
                    }
                }
            }
        }

        // ── Split layout: 70% checklist | 30% sticky weather ──
        Row {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Config.spacingSmall
            spacing: 0

            // ── LEFT: checklist (70%) ──────────────────────────────
            Item {
                width: parent.width * Config.kChecklistPanelRatio
                height: parent.height

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

        // Operator input section — shown at top of checklist page when no operator selected
        Rectangle {
            visible: !OperatorManager.hasCurrentOperator
            width: parent.width
            color: Colors.surface
            radius: Config.radiusSmall
            implicitHeight: operatorCol.implicitHeight
            z: 1
            Layout.fillWidth: true
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            Column {
                id: operatorCol
                anchors.fill: parent
                spacing: 8
                padding: 12

                Label {
                    text: qsTr("Who is conducting this preflight?")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                }

                // Pilot / trainee and instructor names + role + Confirm on a single row
                Row {
                    spacing: 8
                    TextField {
                        id: operatorFirstField
                        placeholderText: qsTr("First name")
                        width: 130
                        font.pixelSize: Config.fontSizeBody
                    }
                    TextField {
                        id: operatorLastField
                        placeholderText: qsTr("Last name")
                        width: 130
                        font.pixelSize: Config.fontSizeBody
                    }
                    TextField {
                        id: instructorFirstField
                        placeholderText: qsTr("Instructor first")
                        width: 130
                        visible: FlightSession.isTraining
                        font.pixelSize: Config.fontSizeBody
                    }
                    TextField {
                        id: instructorLastField
                        placeholderText: qsTr("Instructor last")
                        width: 130
                        visible: FlightSession.isTraining
                        font.pixelSize: Config.fontSizeBody
                    }
                    ComboBox {
                        id: operatorRoleBox
                        model: ["Pilot", "Trainee", "Supervisor", "Inspector"]
                        width: 120
                        currentIndex: FlightSession.isTraining ? 1 : 0
                        font.pixelSize: Config.fontSizeBody
                    }
                    Button {
                        id: confirmOperatorButton
                        text: qsTr("Confirm")
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: Config.fontSizeBody
                        enabled: operatorFirstField.text.trim().length > 0
                            && operatorLastField.text.trim().length > 0
                            && (!FlightSession.isTraining
                                || (instructorFirstField.text.trim().length > 0
                                    && instructorLastField.text.trim().length > 0))
                        onClicked: {
                            var name = (operatorFirstField.text.trim()
                                        + " " + operatorLastField.text.trim())
                            var id = OperatorManager.addOperator(
                                name, operatorRoleBox.currentText)
                            if (id < 0) {
                                OperatorManager.selectByName(name)
                            } else {
                                OperatorManager.selectOperator(id)
                            }
                            if (FlightSession.isTraining) {
                                var iname = (instructorFirstField.text.trim()
                                             + " " + instructorLastField.text.trim())
                                var iid = OperatorManager.addOperator(iname, "Instructor")
                                if (iid < 0) {
                                    for (var i = 0; i < OperatorManager.operators.length; i++) {
                                        if (OperatorManager.operators[i].name === iname) {
                                            iid = OperatorManager.operators[i].id
                                            break
                                        }
                                    }
                                }
                                if (iid > 0) FlightSession.setInstructor(iid)
                            }
                        }
                    }
                }

                Row {
                    visible: OperatorManager.operators.length > 0
                    spacing: 8
                    Label {
                        text: qsTr("or returning operator:")
                        font.pixelSize: Config.fontSizeBody
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        id: returningPicker
                        model: OperatorManager.operators
                        textRole: "name"
                        font.pixelSize: Config.fontSizeBody
                        onActivated: OperatorManager.selectOperator(model[currentIndex].id)
                    }
                }
            }
        }

        // Selected operator summary label
        Label {
            visible: OperatorManager.hasCurrentOperator
            text: FlightSession.isTraining
                  ? qsTr("Trainee: ") + OperatorManager.currentOperatorName
                    + "  ·  " + qsTr("Instructor: ") + FlightSession.instructorName
                  : OperatorManager.currentOperatorName + "  ·  " + OperatorManager.currentOperatorRole
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
        }



        // Scrollable categories
        Flickable {
            id: checklistFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Config.spacingSmall
            clip: true
            contentWidth: width
            contentHeight: scrollCol.height + Config.spacingMedium
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: scrollCol
                width: checklistFlickable.width
                spacing: Config.spacingMedium

                Repeater {
                    model: _engine.catOrder

                    delegate: Column {
                        id: section
                        readonly property int catId: modelData
                        readonly property var info: _engine.catInfo[catId]
                        readonly property var modelObj: catModels[catId]
                        readonly property int catCount: modelObj ? modelObj.count : 0
                        readonly property int failCount: _engine.catFailCount(catId, catModels[catId])
                        readonly property bool hasFail: failCount > 0
                        readonly property real contentWidth: scrollCol.width - Config.spacingMedium * 2

                        width: scrollCol.width
                        spacing: 6
                        visible: catCount > 0

                        // Category header
                        Rectangle {
                            width: section.contentWidth
                            x: Config.spacingMedium
                            height: 34
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: hasFail ? Colors.error : Colors.border
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Config.spacingSmall
                                anchors.rightMargin: Config.spacingSmall
                                spacing: Config.spacingSmall

                                Item { Layout.fillWidth: true }
                                Text { text: info.icon; font.pixelSize: Config.fontSizeBody }
                                Text {
                                    text: info.label
                                    font.pixelSize: Config.fontSizeH3
                                    font.bold: true
                                    color: Colors.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: _engine.catSummary(catId, catModels[catId])
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: hasFail ? Colors.error
                                         : _engine.catWarnCount(catId, catModels[catId]) > 0 ? Colors.checkWarn
                                         : Colors.success
                                }
                            }
                        }

                        // Responsive card grid (2–5 columns)
                        Item {
                            id: gridHost
                            x: Config.spacingMedium
                            width: section.contentWidth
                            height: cardFlow.height

                            readonly property int gridColumns: 3
                            readonly property real cellWidth: gridColumns > 0
                                ? Math.floor((width - (gridColumns - 1) * 6) / gridColumns)
                                : width

                            Flow {
                                id: cardFlow
                                width: gridHost.width
                                spacing: 6

                                Repeater {
                                    model: modelObj

                                    delegate: Rectangle {
                                        id: checkItem

                                        required property string checkId
                                        required property string label
                                        required property int status
                                        required property string message
                                        required property int category
                                        required property int type
                                        required property var currentValue
                                        required property string recommendedAction
                                        required property string userMessage
                                        required property bool isManual
                                        required property bool mandatory
                                        required property var checkObject
                                        required property string rationale
                                        required property var fixSteps
                                        required property string threshold
                                        required property string currentValueString
                                        required property bool isAction
                                        required property string actionButtonText

                                        readonly property bool isGcsFailsafeCheck: checkId === "safety.failsafe.gcs"
                                        width: isGcsFailsafeCheck ? gridHost.width : gridHost.cellWidth
                                        radius: Config.radiusSmall
                                        color: {
                                            if (status === 1) return Colors.successDim
                                            if (status === 2) return Colors.errorDim
                                            if (status === 3) return Colors.checkWarnDim
                                            return Colors.surface
                                        }
                                        border.color: {
                                            if (status === 1) return Colors.success
                                            if (status === 2) return Colors.error
                                            if (status === 3) return Colors.checkWarn
                                            return Colors.borderLight
                                        }
                                        border.width: 1
                                        Behavior on color { ColorAnimation { duration: 150 } }

                                        readonly property bool isMotorSpinCheck: checkId === "propulsion.motors.spin"
                                        property bool expanded: root._expandedChecks[checkId] === true
                                        readonly property int collapsedH: isManual || isAction ? 84 : 64
                                        property string _checkTime: ""
                                        height: isMotorSpinCheck ? (motorPanelLoader.item ? motorPanelLoader.item.implicitHeight + 10 : collapsedH) : (expanded ? expandedCol.implicitHeight + 10 : collapsedH)
                                    ToolTip {
                                        visible: tooltipMa.containsMouse
                                        text: "ID: " + checkId + "\nType: " + _engine.typeNames[type]
                                            + "\nCategory: " + _engine.catInfo[category].label
                                            + "\nMandatory: " + (mandatory ? "Yes" : "No")
                                            + "\nStatus: " + (status === 1 ? "Passed" : status === 2 ? "Failed" : status === 3 ? "Warning" : "Pending")
                                        delay: 600
                                    }

                                    MouseArea {
                                        id: tooltipMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        visible: !isMotorSpinCheck
                                        enabled: !isMotorSpinCheck
                                        onClicked: {
                                            root._expandedChecks[checkId] = !root._expandedChecks[checkId]
                                            root._expandedChecks = root._expandedChecks
                                        }
                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    Loader {
                                        id: motorPanelLoader
                                        anchors.fill: parent
                                        visible: isMotorSpinCheck
                                        active: isMotorSpinCheck
                                        source: active ? "qrc:/qml/cpts/MotorCheckPanel.qml" : ""
                                        onLoaded: {
                                            if (item)
                                                item.checklistCheck = checkObject
                                        }
                                    }

                                    ColumnLayout {
                                        id: expandedCol
                                        visible: !isMotorSpinCheck
                                        width: parent.width
                                        anchors.top: parent.top
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.margins: 8
                                        spacing: 4

                                        // Row 1: icon + label + badge + chevron
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Text {
                                                text: status === 1 ? "\u2713"
                                                    : status === 2 ? "\u2717"
                                                    : status === 3 ? "\u26A0"
                                                    : "\u25CF"
                                                font.pixelSize: Config.fontSizeBody
                                                color: status === 1 ? Colors.success
                                                     : status === 2 ? Colors.error
                                                     : status === 3 ? Colors.checkWarn
                                                     : Colors.textDisabled
                                            }

                                            Text {
                                                text: label
                                                font.pixelSize: Config.fontSizeBody
                                                font.bold: true
                                                color: Colors.textPrimary
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                maximumLineCount: 1
                                            }

                                            Rectangle {
                                                visible: status === 1
                                                color: Colors.success
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: 20
                                                Layout.preferredWidth: 44
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "PASS"
                                                    color: Colors.background
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle {
                                                visible: status === 2
                                                color: Colors.error
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: 20
                                                Layout.preferredWidth: 44
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "FAIL"
                                                    color: Colors.background
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle {
                                                visible: status === 3
                                                color: Colors.checkWarn
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: 20
                                                Layout.preferredWidth: 44
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "WARN"
                                                    color: Colors.background
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                }
                                            }

                                            Text {
                                                text: checkItem.expanded ? "\u25B2" : "\u25BC"
                                                font.pixelSize: 9
                                                color: Colors.textSecondary
                                            }
                                        }

                                        // Row 2: status message
                                        Text {
                                            Layout.fillWidth: true
                                            visible: message.length > 0 && !checkItem.expanded && !isManual
                                            text: message
                                            font.pixelSize: Config.fontSizeBody
                                            color: Colors.textSecondary
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                            Layout.leftMargin: 20
                                        }

                                        // GCS-failsafe policy: chosen per airframe and stored
                                        // in check_config; the GCS never writes the FC parameter.
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 20
                                            spacing: 3
                                            visible: isGcsFailsafeCheck && checkItem.expanded

                                            Text {
                                                text: qsTr("GCS failsafe policy for this airframe")
                                                font.pixelSize: Config.fontSizeBody
                                                font.bold: true
                                                color: Colors.textPrimary
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                Button {
                                                    text: checkObject && checkObject.policy === "rc_primary"
                                                          ? qsTr("RC primary ✓") : qsTr("RC primary")
                                                    highlighted: checkObject && checkObject.policy === "rc_primary"
                                                    font.pixelSize: Config.fontSizeSmall
                                                    Layout.preferredHeight: 28
                                                    onClicked: {
                                                        if (checkObject)
                                                            checkObject.setPolicy("rc_primary")
                                                    }
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: qsTr("RC link is primary; a GCS telemetry dropout must not trigger a failsafe.")
                                                    font.pixelSize: Config.fontSizeSmall
                                                    color: Colors.textSecondary
                                                    wrapMode: Text.WordWrap
                                                }
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                Button {
                                                    text: checkObject && checkObject.policy === "telemetry_primary"
                                                          ? qsTr("Telemetry primary ✓") : qsTr("Telemetry primary")
                                                    highlighted: checkObject && checkObject.policy === "telemetry_primary"
                                                    font.pixelSize: Config.fontSizeSmall
                                                    Layout.preferredHeight: 28
                                                    onClicked: {
                                                        if (checkObject)
                                                            checkObject.setPolicy("telemetry_primary")
                                                    }
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: qsTr("GCS link is primary; losing it must trigger the aircraft's failsafe.")
                                                    font.pixelSize: Config.fontSizeSmall
                                                    color: Colors.textSecondary
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        }

                                        // Row 3: manual / action controls
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: (isManual || isAction) ? actionRow.implicitHeight : 0
                                            visible: isManual || isAction

                                            ColumnLayout {
                                                id: actionRow
                                                width: parent.width
                                                spacing: 4

                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: isManual && message.length > 0 && status !== 1
                                                    text: message
                                                    font.pixelSize: Config.fontSizeBody
                                                    color: Colors.textSecondary
                                                    elide: Text.ElideRight
                                                    maximumLineCount: 1
                                                    Layout.leftMargin: 20
                                                }

                                                Rectangle {
                                                    id: manualBtn
                                                    visible: isManual
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 32
                                                    Layout.leftMargin: 12
                                                    Layout.rightMargin: 12
                                                    radius: Config.radiusSmall
                                                    color: status === 1 ? Colors.success : Colors.surface
                                                    border.color: status === 1 ? Colors.success : Colors.border
                                                    border.width: 1

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: actionButtonText.length > 0 ? actionButtonText : "Mark as Checked"
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: status === 1
                                                        color: status === 1 ? Colors.background : Colors.textPrimary
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        onClicked: {
                                                            if (status === 1) return
                                                            var chk = checkObject
                                                            if (chk) {
                                                                chk.confirm("Operator confirmed")
                                                                checkItem._checkTime = _engine.timestamp()
                                                            }
                                                        }
                                                        onPressAndHold: {
                                                            if (status !== 1) return
                                                            var chk = checkObject
                                                            if (chk) {
                                                                chk.reset()
                                                                checkItem._checkTime = ""
                                                            }
                                                        }
                                                        cursorShape: Qt.PointingHandCursor
                                                    }
                                                }

                                                Text {
                                                    visible: isManual && status === 1 && checkItem._checkTime.length > 0
                                                    text: "Checked at " + checkItem._checkTime
                                                    font.pixelSize: Config.fontSizeSmall
                                                    color: Colors.success
                                                    Layout.alignment: Qt.AlignHCenter
                                                }

                                                ActionCheckCard {
                                                    id: actionCard
                                                    visible: isAction
                                                    check: checkObject
                                                    readonly property bool isPackCheck: checkId === "power.battery.pack"
                                                    readonly property bool isPayloadCheck: checkId === "airframe.payload"
                                                    readonly property bool isMotorTableCheck: checkId === "power.power_type"
                                                    readonly property bool isMissionEstimate: checkId === "power.mission_estimate"
                                                    actionLabel: isPackCheck ? (status === 1 ? qsTr("Change battery pack") : qsTr("Select battery pack"))
                                                                 : isPayloadCheck ? (status === 1 ? qsTr("Change payload") : qsTr("Enter payload"))
                                                                 : isMotorTableCheck ? qsTr("Motor table")
                                                                 : isMissionEstimate ? qsTr("Mission estimate details")
                                                                 : actionButtonText.length > 0 ? actionButtonText : "Run"
                                                    repeatable: isPackCheck || isPayloadCheck || isMotorTableCheck || isMissionEstimate
                                                    running: root._runningChecks[checkId] === true
                                                    Layout.fillWidth: true
                                                    Layout.leftMargin: 8
                                                    Layout.rightMargin: 8

                                                    onActionTriggered: function(cid) {
                                                        if (isPackCheck) {
                                                            batteryPackDialog.open()
                                                            return
                                                        }
                                                        if (isPayloadCheck) {
                                                            payloadDialog.open()
                                                            return
                                                        }
                                                        if (isMotorTableCheck) {
                                                            motorTableDialog.open()
                                                            return
                                                        }
                                                        if (isMissionEstimate) {
                                                            missionEstimateDialog.open()
                                                            return
                                                        }
                                                        root._runningChecks[cid] = true
                                                        root._runningChecks = root._runningChecks
                                                        var chk = checkObject
                                                        if (chk) chk.evaluate()
                                                    }
                                                }

                                                Connections {
                                                    target: checkObject
                                                    enabled: isAction && checkObject
                                                    function onStatusChanged(cid, newStatus) {
                                                        if (cid !== checkId)
                                                            return
                                                        root._runningChecks[cid] = false
                                                        root._runningChecks = root._runningChecks
                                                    }
                                                }
                                            }
                                        }

                                        // Expanded detail
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            visible: checkItem.expanded
                                            Layout.topMargin: 4

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 2
                                                color: status === 1 ? Colors.success
                                                     : status === 2 ? Colors.error
                                                     : status === 3 ? Colors.checkWarn
                                                     : Colors.borderLight
                                                radius: 1
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                color: Colors.surfaceLight
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: expandedContent.implicitHeight + 8

                                                ColumnLayout {
                                                    id: expandedContent
                                                    x: 6
                                                    y: 4
                                                    width: parent.width - 12
                                                    spacing: 4

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: currentValueString.length > 0 && currentValueString !== "0"
                                                        Text { text: "Current:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                        Text {
                                                            text: currentValueString
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: Colors.textPrimary
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                        }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: threshold.length > 0
                                                        Text { text: "Required:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                        Text {
                                                            text: threshold
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: Colors.textPrimary
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                        }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: rationale.length > 0
                                                        Text { text: "Why:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                        Text {
                                                            text: rationale
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: Colors.textPrimary
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 4
                                                        }
                                                    }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        visible: fixSteps && fixSteps.length > 0
                                                        Text { text: "How to fix:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                                                        Repeater {
                                                            model: fixSteps
                                                            delegate: RowLayout {
                                                                required property int index
                                                                required property string modelData
                                                                Layout.fillWidth: true
                                                                spacing: 4
                                                                Text {
                                                                    text: (index + 1) + "."
                                                                    font.pixelSize: Config.fontSizeSmall
                                                                    color: Colors.accent
                                                                }
                                                                Text {
                                                                    text: modelData
                                                                    font.pixelSize: Config.fontSizeSmall
                                                                    color: Colors.textPrimary
                                                                    Layout.fillWidth: true
                                                                    wrapMode: Text.WordWrap
                                                                    maximumLineCount: 2
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                }

                Item { width: 1; height: Config.spacingSmall }

                // ── Next → Gimbal Test (bottom of the checks, after the last
                // category). Navigation only — not an arming action — so it is
                // never gated on pending/failed checks. Arming stays blocked by
                // ArmingGate / the ArmGatePanel footer. GimbalTest hides its own
                // Control Surface sweep for multirotors; Restricted Zones stays.
                Rectangle {
                    id: nextGimbalBtn
                    width: scrollCol.width - Config.spacingMedium * 2
                    x: Config.spacingMedium
                    height: 40
                    radius: 8
                    visible: root._showNextBtn
                    color: Colors.primary
                    opacity: nextBtnMa.containsMouse ? 0.9 : 1.0

                    Text {
                        id: nextBtnLbl
                        anchors.centerIn: parent
                        text: "Next: Gimbal Test  →"
                        font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textInverse
                    }

                    MouseArea {
                        id: nextBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._leftView = "gimbal"
                    }
                }
            }
        }
                }
                // ── Gimbal test overlay (inline; replaces the checklist when active) ────
                // Hosted inline rather than pushed on a page stack — Config.navigatePush()
                // is a no-op here (no mainStackView). This mirrors how PreFlightChecklist.qml
                // hosts the same page. GimbalTest hides its own Control Surface sweep for
                // multirotors and keeps the Restricted Zones airspace panel for all vehicles.
                Rectangle {
                    id: gimbalOverlay
                    anchors.fill: parent
                    z: 50
                    visible: root._leftView === "gimbal"
                    color: Colors.background

                    // Title bar (top)
                    Rectangle {
                        id: gimbalTitleBar
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 44
                        z: 1
                        color: Colors.surface

                        Text {
                            anchors.centerIn: parent
                            text: "🎯  Gimbal Test"
                            font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary
                        }
                    }

                    Loader {
                        id: gimbalLoader
                        anchors.top: gimbalTitleBar.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: gimbalBackBar.top
                        source: gimbalOverlay.visible ? "qrc:/qml/pages/GimbalTest.qml" : ""
                        onStatusChanged: {
                            if (status === Loader.Error)
                                console.warn("[PreflightChecklistView] failed to load GimbalTest.qml")
                        }
                    }

                    // Back bar (footer, at the bottom — matches the Next button position)
                    Rectangle {
                        id: gimbalBackBar
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 52
                        z: 1
                        color: Colors.surface

                        // Same look as "Next: Gimbal Test" on the checklist: wide,
                        // centered, primary colour.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Config.spacingMedium
                            anchors.rightMargin: Config.spacingMedium
                            anchors.verticalCenter: parent.verticalCenter
                            height: 40
                            radius: 8
                            color: Colors.primary
                            opacity: backBtnMa.containsMouse ? 0.9 : 1.0

                            Text {
                                id: backBtnLbl
                                anchors.centerIn: parent
                                text: "←  Back to Checklist"
                                font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textInverse
                            }

                            MouseArea {
                                id: backBtnMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root._leftView = "checklist"
                            }
                        }
                    }
                }
            }

            // ── RIGHT: weather panel (30%, sticky) ─────────────────
            Item {
                width: parent.width * (1.0 - Config.kChecklistPanelRatio)
                height: parent.height

                Flickable {
                    anchors.fill: parent
                    clip: true

                    WeatherInfoPanel {
                        anchors.fill: parent
                        anchors.leftMargin: Config.spacingSmall
                        anchors.topMargin: Config.spacingSmall
                    }
                }
            }
        }

        // Fixed footer
        ArmGatePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: Config.kFooterHeight
        }
    }
}
