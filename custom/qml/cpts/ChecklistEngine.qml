import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

QtObject {
    property string viewMode: "analyze"

    readonly property var catInfo: ({
        0: { label: "Propulsion",      icon: "\u2699" },
        1: { label: "Power",           icon: "\u26A1" },
        2: { label: "GPS/Navigation",  icon: "\uD83D\uDEE0" },
        3: { label: "Communication",   icon: "\uD83D\uDCF6" },
        4: { label: "Airframe",        icon: "\uD83D\uDEE9" },
        5: { label: "Safety",          icon: "\uD83D\uDEE1" },
        6: { label: "Environment",     icon: "\uD83C\uDF2C" },
        7: { label: "Arming Gate",     icon: "\u2699" }
    })

    readonly property var catOrder: [1, 2, 3, 4, 5, 0]

    readonly property var catGroups: [
        { label: "Power \u0026 Propulsion",     icon: "\u26A1", cats: [0, 1], accent: Colors.pastelGreen },
        { label: "GPS/Navigation \u0026 Sensors", icon: "\uD83D\uDEE0", cats: [2],    accent: Colors.pastelBlue },
        { label: "Communication \u0026 Control", icon: "\uD83D\uDCF6", cats: [3],    accent: Colors.pastelPurple },
        { label: "Airframe \u0026 Physical",    icon: "\uD83D\uDEE9", cats: [4],    accent: Colors.pastelPink },
        { label: "Safety \u0026 Failsafes",     icon: "\uD83D\uDEE1", cats: [5],    accent: Colors.warning }
    ]

    readonly property var typeNames: ["Auto", "Manual", "Action"]

    function _checks() {
        return typeof PreflightManager !== "undefined" ? PreflightManager.checks : []
    }

    function modelChecks(model) {
        if (model) {
            var out = []
            for (var i = 0; i < model.count; ++i)
                out.push(model.get(i))
            return out
        }
        return _checks()
    }

    function catFailCount(catId, model) {
        var arr = modelChecks(model)
        if (model) {
            var n = 0
            for (var i = 0; i < arr.length; ++i) {
                if (arr[i].status === 2) n++
            }
            return n
        }
        var n = 0
        for (var i = 0; i < arr.length; ++i) {
            var c = arr[i]
            if (c.mandatory && c.checkCategory === catId && (c.status === 2 || c.status === 4))
                n++
        }
        return n
    }

    function catWarnCount(catId, model) {
        var arr = modelChecks(model)
        if (model) {
            var n = 0
            for (var i = 0; i < arr.length; ++i) {
                if (arr[i].status === 3) n++
            }
            return n
        }
        var n = 0
        for (var i = 0; i < arr.length; ++i) {
            var c = arr[i]
            if (c.checkCategory === catId && c.status === 3)
                n++
        }
        return n
    }

    function catPassCount(catId, model) {
        var arr = modelChecks(model)
        if (model) {
            var n = 0
            for (var i = 0; i < arr.length; ++i) {
                if (arr[i].status === 1) n++
            }
            return n
        }
        var n = 0
        for (var i = 0; i < arr.length; ++i) {
            var c = arr[i]
            if (c.checkCategory === catId && c.status === 1)
                n++
        }
        return n
    }

    function catTotal(catId, model) {
        var arr = modelChecks(model)
        if (model) return arr.length
        var n = 0
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].checkCategory === catId)
                n++
        }
        return n
    }

    function catSummary(catId, model) {
        if (model && model.count === 0) return ""
        var t = catTotal(catId, model)
        if (t === 0) return ""
        var f = catFailCount(catId, model)
        if (f > 0) return f + " FAIL"
        var w = catWarnCount(catId, model)
        if (w > 0) return w + " WARN"
        return catPassCount(catId, model) + "/" + t + " passed"
    }

    function groupIcon(idx) {
        var arr = catGroups
        if (idx < 0 || idx >= arr.length) return ""
        var g = arr[idx]
        var checks = _checks()
        var allPassed = true
        var anyFail = false
        var anyPending = false
        for (var i = 0; i < checks.length; ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    if (c.mandatory && (c.status === 0 || c.status === 3)) { allPassed = false; anyPending = true }
                    if (c.mandatory && (c.status === 2 || c.status === 4)) { allPassed = false; anyFail = true }
                }
            }
        }
        if (allPassed) return "\u2713"
        if (anyFail) return "\u2717"
        return "\u25CF"
    }

    function groupColor(idx) {
        var arr = catGroups
        if (idx < 0 || idx >= arr.length) return Colors.textSecondary
        var g = arr[idx]
        var checks = _checks()
        var anyFail = false
        var anyPending = false
        for (var i = 0; i < checks.length; ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    if (c.mandatory && (c.status === 2 || c.status === 4)) anyFail = true
                    if (c.mandatory && (c.status === 0 || c.status === 3)) anyPending = true
                }
            }
        }
        if (anyFail) return Colors.error
        if (anyPending) return Colors.warning
        return Colors.success
    }

    function groupPassedCount(idx) {
        var arr = catGroups
        if (idx < 0 || idx >= arr.length) return "0/0"
        var g = arr[idx]
        var checks = _checks()
        var passed = 0, total = 0
        for (var i = 0; i < checks.length; ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    total++
                    if (c.status === 1) passed++
                }
            }
        }
        return passed + "/" + total
    }

    readonly property int totalChecks: {
        var arr = _checks()
        if (!arr) return 0
        var n = 0
        for (var i = 0; i < arr.length; ++i) {
            if (!arr[i].isInformational) n++
        }
        return n
    }

    readonly property int criticalChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].mandatory && (arr[i].status === 2 || arr[i].status === 4)) n++
        }
        return n
    }

    readonly property int pendingChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].isInformational) continue
            if (arr[i].status === 0 || arr[i].status === 3) n++
        }
        return n
    }

    readonly property int passedChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].isInformational) continue
            if (arr[i].status === 1) n++
        }
        return n
    }

    readonly property int completionPercent: {
        if (typeof PreflightChecklistModel === "undefined") return 0
        return PreflightChecklistModel.completionPercent
    }

    function timestamp() {
        var d = new Date()
        return d.getHours().toString().padStart(2, "0") + ":"
            + d.getMinutes().toString().padStart(2, "0") + ":"
            + d.getSeconds().toString().padStart(2, "0")
    }

    // Enter what the aircraft carries this flight (the "Payload" item, both
    // checklist pages). Weight only, in kg; the aircraft's own weight is set in
    // Vehicle Management. 0 = no payload. The weight goes on the flight record
    // at arm. An inline component, so no new qrc entry.
    // The look of the preflight dialogs below: light text on the dark panel,
    // dark text on the light buttons (Qt's stock palette drew dark text on
    // the dark panel), and a 10 % larger font. Controls inside take both.
    component SkyDialog: Dialog {
        font.pointSize: Qt.application.font.pointSize * 1.1
        palette.window:          Colors.surface          // panel: light text on it
        palette.windowText:      Colors.textPrimary      // labels, flat buttons, the selected tab
        palette.base:            Colors.bgPrimary        // input fields
        palette.alternateBase:   Colors.surfaceLight
        palette.text:            Colors.textPrimary
        palette.placeholderText: Colors.textSecondary
        palette.button:          Colors.textPrimary      // light buttons with dark text
        palette.buttonText:      Colors.background
        palette.dark:            Colors.teal             // highlighted buttons, unselected tabs
        palette.brightText:      Colors.textInverse      // text on teal
        palette.mid:             Colors.textSecondary
        palette.midlight:        Colors.divider
        palette.light:           Colors.divider
        palette.shadow:          Colors.background
        palette.highlight:       Colors.teal
        palette.highlightedText: Colors.textInverse
        palette.toolTipBase:     Colors.bgPrimary
        palette.toolTipText:     Colors.textPrimary
    }

    component PayloadDialog: SkyDialog {
        id: payloadDialog
        title:            qsTr("Payload")
        modal:            true
        closePolicy:      Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:            Math.min(380, (parent ? parent.width : 380) - 32)

        readonly property var _vpm: (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager : null
        readonly property real _kg: Number(kgField.text.replace(",", "."))
        readonly property bool _valid: kgField.text.trim().length > 0 && isFinite(_kg) && _kg >= 0 && _kg <= 100 && _minValid
        // Time at the target (the trip to a target, when no mission is
        // loaded): optional; empty = not entered.
        readonly property real _min: Number(minField.text.replace(",", "."))
        readonly property bool _minValid: minField.text.trim().length === 0 || (isFinite(_min) && _min >= 0 && _min <= 600)

        onAboutToShow: {
            kgField.text = (_vpm && _vpm.payloadEntered) ? _vpm.currentPayloadWeightKg.toFixed(2) : ""
            minField.text = (_vpm && _vpm.timeAtTargetMin >= 0) ? String(_vpm.timeAtTargetMin) : ""
            kgField.forceActiveFocus()
        }

        function _save(kg) {
            if (!_vpm || !_minValid)
                return
            if (minField.text.trim().length > 0 && !_vpm.enterTimeAtTargetMin(_min))
                return
            if (_vpm.enterPayloadWeightKg(kg))
                payloadDialog.close()
        }

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Weight of what the aircraft carries on this flight (camera, gimbal, cargo), "
                           + "not the aircraft itself. It is saved on the flight record.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Payload (kg):") }
                TextField {
                    id: kgField
                    objectName: "payloadKgField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. 1.2")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onAccepted: if (payloadDialog._valid) payloadDialog._save(payloadDialog._kg)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Time at target (min):") }
                TextField {
                    id: minField
                    objectName: "payloadMinField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("copters: e.g. 2")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Without a mission, the payload is dropped at the target and the time left counts the way back "
                           + "without it. With a mission, set where it is dropped in the Mission estimate.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textSecondary
            }

            Text {
                visible: !payloadDialog._minValid
                Layout.fillWidth: true
                text: qsTr("Enter a time at target between 0 and 600 minutes (or leave it empty).")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.error
                wrapMode: Text.WordWrap
            }

            Text {
                visible: kgField.text.trim().length > 0 && !payloadDialog._valid && payloadDialog._minValid
                Layout.fillWidth: true
                text: qsTr("Enter a weight between 0 and 100 kg.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.error
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Button {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: payloadDialog.close()
                }
                Button {
                    text: qsTr("No payload")
                    Layout.fillWidth: true
                    onClicked: payloadDialog._save(0)
                }
                Button {
                    text: qsTr("Save")
                    highlighted: true
                    enabled: payloadDialog._valid
                    Layout.fillWidth: true
                    onClicked: payloadDialog._save(payloadDialog._kg)
                }
            }
        }
    }

    // The "Motor table" item (copters): shows the maker's motor table the
    // copter is linked to (the link itself is made on Analyze → Vehicles →
    // Motor table), or lets the pilot fly this flight without one (logged
    // with the pilot's name).
    component MotorTableDialog: SkyDialog {
        id: ptDialog
        title:            qsTr("Motor Table")
        modal:            true
        closePolicy:      Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:            Math.min(440, (parent ? parent.width : 440) - 32)

        readonly property var _vpm: (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager : null
        property string _tableName: ""
        readonly property bool _onTable: _vpm !== null && _vpm.motorTableId !== ""

        function _reload() {
            _tableName = ""
            var tables = _vpm ? _vpm.motorTables() : []
            for (var t = 0; t < tables.length; ++t)
                if (tables[t].id === _vpm.motorTableId) _tableName = tables[t].name
        }

        onAboutToShow: {
            ptWaiveBox.checked = false
            ptPilotField.text = (typeof OperatorManager !== "undefined" && OperatorManager)
                    ? OperatorManager.currentOperatorName : ""
            _reload()
        }

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            Text {
                objectName: "ptDialogText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: ptDialog._onTable
                      ? qsTr("This copter's flight-time estimate and mission estimate come from the maker's motor "
                             + "table: %1. Change the link on Analyze → Vehicles → Motor table.").arg(ptDialog._tableName)
                      : qsTr("Not linked: link this copter to its maker's motor table on Analyze → Vehicles → "
                             + "Motor table, or fly without one below.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textPrimary
            }

            RowLayout {
                visible: !ptDialog._onTable
                Layout.fillWidth: true
                CheckBox { id: ptWaiveBox; objectName: "ptWaiveBox" }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Fly without a motor table. I accept a rough flight-time estimate and no mission estimate.")
                    font.pixelSize: Config.fontSizeDialog
                    color: Colors.textPrimary
                    MouseArea { anchors.fill: parent; onClicked: ptWaiveBox.toggle() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: ptWaiveBox.checked
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pilot:") }
                TextField { id: ptPilotField; objectName: "ptPilotField"; Layout.fillWidth: true; placeholderText: qsTr("pilot name") }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Button { text: ptWaiveBox.checked ? qsTr("Cancel") : qsTr("Close"); Layout.fillWidth: true; onClicked: ptDialog.close() }
                Button {
                    objectName: "ptWaiveButton"
                    visible: ptWaiveBox.checked
                    text: qsTr("Fly without a table")
                    enabled: ptPilotField.text.trim().length > 0
                    Layout.fillWidth: true
                    onClicked: if (ptDialog._vpm && ptDialog._vpm.waiveMotorTable(ptPilotField.text.trim())) ptDialog.close()
                }
            }
        }
    }

    // The mission estimate for the mission on the copter: the result, the
    // landing charge, each waypoint's time and charge on arrival (likely and
    // pessimistic) and state, the reasons, what is assumed and the weather
    // used. The empty weight and the RC drop item are entered here. Advice
    // only: nothing here holds arming. The waypoint list is one rich-text
    // table (a Repeater inside a page-level dialog's inline component crashed
    // the checklist pages in Qt 6.8).
    component MissionEstimateDialog: SkyDialog {
        id: meDialog
        title:            qsTr("Mission Estimate")
        modal:            true
        closePolicy:      Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:            Math.min(560, (parent ? parent.width : 560) - 32)

        readonly property var _vpm: (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager : null
        readonly property var _settings: (typeof PreflightSettingsManager !== "undefined") ? PreflightSettingsManager : null
        property var _r: ({})

        function refresh() { _r = _vpm ? _vpm.missionEstimate() : ({}) }
        function _pct(v) { return (v === undefined || isNaN(v)) ? "--" : Math.round(v) + " %" }
        function _colour(c) {
            return c === "green" ? Colors.statusPass : c === "amber" ? Colors.statusWarn
                 : c === "red" ? Colors.statusFail : Colors.textSecondary
        }

        onAboutToShow: {
            var kg = _vpm ? _vpm.emptyWeightKg() : -1
            meWeightField.text = kg > 0 ? Number(kg).toFixed(2) : ""
            refresh()
        }

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            // The result and the landing charge.
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "meResult"
                    text: (meDialog._r.resultName || "").toUpperCase()
                    font.pixelSize: Config.fontSizeH3
                    font.weight: Font.Bold
                    color: meDialog._colour(meDialog._r.colour)
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("Confidence: %1").arg(meDialog._r.confidence || "--")
                          + ((meDialog._r.confidenceWhy || []).length > 0 ? " (" + meDialog._r.confidenceWhy.join(", ") + ")" : "")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: meDialog.width * 0.6
                    horizontalAlignment: Text.AlignRight
                }
            }
            Text {
                Layout.fillWidth: true
                visible: meDialog._r.ready === true
                text: qsTr("Lands with %1 (pessimistic %2) · total %3")
                          .arg(meDialog._pct(meDialog._r.landLikelyPct)).arg(meDialog._pct(meDialog._r.landPessimisticPct))
                          .arg(meDialog._r.totalText || "--")
                font.pixelSize: Config.fontSizeDialog
                font.weight: Font.DemiBold
                color: Colors.textPrimary
            }
            Text {
                Layout.fillWidth: true
                visible: meDialog._r.ready !== true
                wrapMode: Text.WordWrap
                text: qsTr("Can't assess: %1").arg(meDialog._r.problem || "")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.statusWarn
            }

            // The weights and the drop.
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text { text: qsTr("Weight without payload (kg)"); font.pixelSize: Config.fontSizeDialog; color: Colors.textSecondary }
                TextField {
                    id: meWeightField
                    objectName: "meWeightField"
                    Layout.preferredWidth: 90
                    placeholderText: qsTr("kg")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                Button {
                    objectName: "meWeightSave"
                    text: qsTr("Save")
                    enabled: parseFloat(meWeightField.text) > 0
                    onClicked: if (meDialog._vpm && meDialog._vpm.setEmptyWeightKg(parseFloat(meWeightField.text))) meDialog.refresh()
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: meDialog._r.payloadKg > 0
                          ? qsTr("Payload %1 kg in %2 piece(s) · RC drop at item")
                                .arg(Number(meDialog._r.payloadKg).toFixed(1)).arg(meDialog._r.payloadCount || 1)
                          : qsTr("No payload entered (Payload item) · RC drop at item")
                    font.pixelSize: Config.fontSizeDialog
                    color: Colors.textSecondary
                }
                SpinBox {
                    objectName: "meDropItem"
                    from: 0; to: 999
                    value: meDialog._settings ? meDialog._settings.payloadDropItem : 0
                    onValueModified: if (meDialog._settings) { meDialog._settings.payloadDropItem = value; meDialog.refresh() }
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: meDialog._r.dropText || ""
                visible: text.length > 0
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }

            // Each waypoint: time, charge on arrival (pessimistic), state.
            Text {
                objectName: "meWaypoints"
                visible: (meDialog._r.waypoints || []).length > 0
                Layout.fillWidth: true
                textFormat: Text.RichText
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textPrimary
                text: {
                    var w = meDialog._r.waypoints || []
                    var html = "<table width='100%' cellspacing='2'><tr><th align='left'>" + qsTr("Item") + "</th><th align='left'>"
                             + qsTr("What") + "</th><th align='right'>" + qsTr("Time") + "</th><th align='right'>"
                             + qsTr("Charge (pess.)") + "</th><th align='left'>&nbsp;" + qsTr("State") + "</th></tr>"
                    for (var i = 0; i < w.length; ++i) {
                        var st = w[i].state
                        var c = st === 0 ? Colors.statusPass : st === 1 ? Colors.statusWarn : Colors.statusFail
                        html += "<tr><td>" + (w[i].seq >= 0 ? w[i].seq : "–") + "</td><td>" + w[i].what + "</td><td align='right'><tt>"
                              + w[i].timeText + "</tt></td><td align='right'><tt>" + meDialog._pct(w[i].socLikelyPct) + " ("
                              + meDialog._pct(w[i].socPessimisticPct) + ")</tt></td><td><font color='" + c + "'>&nbsp;"
                              + w[i].stateName + "</font></td></tr>"
                    }
                    return html + "</table>"
                }
            }

            // Reasons, assumptions and the weather used.
            Text {
                objectName: "meReasons"
                Layout.fillWidth: true
                visible: (meDialog._r.reasons || []).length > 0
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                font.pixelSize: Config.fontSizeSmall
                color: meDialog._r.weatherRed ? Colors.statusFail : Colors.textPrimary
                text: qsTr("<b>Reasons</b><br>") + (meDialog._r.reasons || []).join("<br>")
            }
            Text {
                Layout.fillWidth: true
                visible: (meDialog._r.assumed || []).length > 0
                wrapMode: Text.WordWrap
                text: qsTr("Assumed: %1").arg((meDialog._r.assumed || []).join("; "))
                font.pixelSize: Config.fontSizeSmall
                font.italic: true
                color: Colors.textSecondary
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Weather: %1").arg(meDialog._r.weatherText || "--")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Advice only: this estimate never holds arming.")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Button { text: qsTr("Refresh"); Layout.fillWidth: true; onClicked: meDialog.refresh() }
                Button { text: qsTr("Close"); Layout.fillWidth: true; onClicked: meDialog.close() }
            }
        }
    }

    // Pick (or register) the battery pack fitted for this
    // flight. Used by both checklist pages (PreflightChecklistView and
    // pages/PreFlightChecklist). Choosing a pack stores its rated config,
    // makes it the current pack for the flight log and cycle count, and feeds
    // the flight-time estimate. An inline component, so no new qrc entry.
    component BatteryPackDialog: SkyDialog {
        id: packDialog
        title:            qsTr("Battery Pack")
        modal:            true
        closePolicy:      Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:            Math.min(440, (parent ? parent.width : 440) - 32)

        // Registered packs, newest first; index packs.length in the combo is "+ New pack".
        property var packs: []
        readonly property bool isNew: packCombo.currentIndex >= packs.length
        readonly property string serialText: serialField.text.trim()
        readonly property bool serialTaken: isNew && packs.some(function(p) { return p.serial === serialText })
        readonly property bool canUse: serialText.length > 0 && !serialTaken && capacitySpin.value > 0

        function _reload() {
            packs = (typeof Database !== "undefined" && Database) ? Database.listBatteryPacks() : []
            var current = (typeof VehicleProfileManager !== "undefined" && VehicleProfileManager)
                    ? VehicleProfileManager.currentBatterySerial : ""
            var idx = packs.length
            for (var i = 0; i < packs.length; ++i)
                if (packs[i].serial === current) { idx = i; break }
            packCombo.currentIndex = idx
            _fill()
        }

        function _fill() {
            if (isNew) {
                serialField.text = ""
                labelField.text = ""
                typeCombo.currentIndex = 0
                seriesSpin.value = 6
                parallelSpin.value = 1
                capacitySpin.value = 5000
                return
            }
            var p = packs[packCombo.currentIndex]
            serialField.text = p.serial
            labelField.text = p.label
            var t = typeCombo.model.indexOf(p.chemistry)
            typeCombo.currentIndex = t >= 0 ? t : 0
            seriesSpin.value = p.series > 0 ? p.series : 6
            parallelSpin.value = p.parallel > 0 ? p.parallel : 1
            capacitySpin.value = p.cellMah > 0 ? p.cellMah : 5000
        }

        onAboutToShow: {
            waiveBox.checked = false
            waivePilotField.text = (typeof OperatorManager !== "undefined" && OperatorManager)
                    ? OperatorManager.currentOperatorName : ""
            _reload()
        }

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Choose the pack fitted for this flight. A flight that uses at least 20% of the "
                           + "pack's rated capacity counts as one cycle.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pack:") }
                ComboBox {
                    id: packCombo
                    Layout.fillWidth: true
                    model: packDialog.packs.map(function(p) {
                        return (p.label ? p.label + " — " : "") + p.serial + "  (" + p.cycles + " cycles)"
                    }).concat([qsTr("+ New pack")])
                    onActivated: packDialog._fill()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pack ID / serial:") }
                TextField {
                    id: serialField
                    Layout.fillWidth: true
                    enabled: packDialog.isNew
                    placeholderText: qsTr("as written on the pack, e.g. PK-07")
                }
            }

            Text {
                visible: packDialog.serialTaken
                Layout.fillWidth: true
                text: qsTr("A pack with this ID is already registered — pick it from the list.")
                font.pixelSize: Config.fontSizeDialog
                color: Colors.error
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Name (optional):") }
                TextField { id: labelField; Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Type:") }
                ComboBox { id: typeCombo; Layout.fillWidth: true; model: ["LiPo", "LiHV", "LiIon", "LiFe"] }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Series (S):") }
                SpinBox { id: seriesSpin; Layout.fillWidth: true; from: 1; to: 24; editable: true; value: 6 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Parallel (P):") }
                SpinBox { id: parallelSpin; Layout.fillWidth: true; from: 1; to: 16; editable: true; value: 1 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Cell capacity (mAh):") }
                SpinBox {
                    id: capacitySpin
                    Layout.fillWidth: true
                    from: 100; to: 100000; stepSize: 500; editable: true; value: 5000
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Rated pack capacity: %1 mAh").arg(capacitySpin.value * parallelSpin.value)
                font.pixelSize: Config.fontSizeDialog
                color: Colors.textSecondary
            }

            // No registered pack for this flight: the pilot can waive the pack
            // and fly without a flight-time estimate. The waiver is logged
            // with the pilot's name and lasts until the flight ends (bench
            // motor and control-surface test arms keep it).
            RowLayout {
                Layout.fillWidth: true
                CheckBox { id: waiveBox }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Fly without a registered pack. I accept that there is no battery "
                               + "flight-time estimate and no cycle is counted.")
                    font.pixelSize: Config.fontSizeDialog
                    color: Colors.textPrimary
                    MouseArea { anchors.fill: parent; onClicked: waiveBox.toggle() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: waiveBox.checked
                Text { font.pixelSize: Config.fontSizeDialog; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pilot:") }
                TextField {
                    id: waivePilotField
                    Layout.fillWidth: true
                    placeholderText: qsTr("pilot name")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Button {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: packDialog.close()
                }
                Button {
                    visible: waiveBox.checked
                    text: qsTr("Fly without a pack")
                    enabled: waivePilotField.text.trim().length > 0
                    Layout.fillWidth: true
                    onClicked: {
                        if (VehicleProfileManager.waiveBatteryPack(waivePilotField.text.trim()))
                            packDialog.close()
                    }
                }
                Button {
                    visible: !waiveBox.checked
                    text: packDialog.isNew ? qsTr("Save && use this pack") : qsTr("Use this pack")
                    highlighted: true
                    enabled: packDialog.canUse
                    Layout.fillWidth: true
                    onClicked: {
                        var serial = packDialog.serialText
                        var label = labelField.text.trim()
                        if (!Database.saveBatteryPack(serial, label, typeCombo.currentText,
                                                      seriesSpin.value, parallelSpin.value, capacitySpin.value))
                            return
                        VehicleProfileManager.setBatterySerial(serial, label)
                        // The flight-time estimate uses the fitted pack.
                        VehicleProfileManager.batteryType = typeCombo.currentText
                        VehicleProfileManager.batterySeriesCells = seriesSpin.value
                        VehicleProfileManager.batteryParallelCells = parallelSpin.value
                        VehicleProfileManager.batteryCellMah = capacitySpin.value
                        packDialog.close()
                    }
                }
            }
        }
    }
}
