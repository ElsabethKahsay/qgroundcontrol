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

    // Step 3 item 7: pick (or register) the battery pack fitted for this
    // flight. Used by both checklist pages (PreflightChecklistView and
    // pages/PreFlightChecklist). Choosing a pack stores its rated config,
    // makes it the current pack for the flight log and cycle count, and feeds
    // the flight-time estimate. An inline component, so no new qrc entry.
    component BatteryPackDialog: Dialog {
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

        onAboutToShow: _reload()

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Choose the pack fitted for this flight. A flight that uses at least 20% of the "
                           + "pack's rated capacity counts as one cycle.")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pack:") }
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
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Pack ID / serial:") }
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
                font.pixelSize: Config.fontSizeSmall
                color: Colors.error
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Name (optional):") }
                TextField { id: labelField; Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Type:") }
                ComboBox { id: typeCombo; Layout.fillWidth: true; model: ["LiPo", "LiHV", "LiIon", "LiFe"] }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Series (S):") }
                SpinBox { id: seriesSpin; Layout.fillWidth: true; from: 1; to: 24; editable: true; value: 6 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Parallel (P):") }
                SpinBox { id: parallelSpin; Layout.fillWidth: true; from: 1; to: 16; editable: true; value: 1 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { font.pixelSize: Config.fontSizeSmall; font.weight: Font.DemiBold; color: Colors.textSecondary; Layout.preferredWidth: 120; text: qsTr("Cell capacity (mAh):") }
                SpinBox {
                    id: capacitySpin
                    Layout.fillWidth: true
                    from: 100; to: 100000; stepSize: 500; editable: true; value: 5000
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Rated pack capacity: %1 mAh").arg(capacitySpin.value * parallelSpin.value)
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
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
