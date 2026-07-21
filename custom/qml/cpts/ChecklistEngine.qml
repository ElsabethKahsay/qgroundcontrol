import QtQuick
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

    readonly property var catOrder: [1, 2, 3, 4, 5, 6, 0, 7]

    readonly property var catGroups: [
        { label: "Power \u0026 Propulsion",     icon: "\u26A1", cats: [0, 1], accent: Colors.pastelGreen },
        { label: "GPS/Navigation \u0026 Sensors", icon: "\uD83D\uDEE0", cats: [2],    accent: Colors.pastelBlue },
        { label: "Communication \u0026 Control", icon: "\uD83D\uDCF6", cats: [3],    accent: Colors.pastelPurple },
        { label: "Airframe \u0026 Physical",    icon: "\uD83D\uDEE9", cats: [4],    accent: Colors.pastelPink },
        { label: "Safety \u0026 Failsafes",     icon: "\uD83D\uDEE1", cats: [5],    accent: Colors.warning },
        { label: "Environment \u0026 Mission",  icon: "\uD83C\uDF2C", cats: [6, 7], accent: Colors.info }
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
            if (c.checkCategory === catId && (c.status === 2 || c.status === 4))
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
                    if (c.status === 0 || c.status === 3) { allPassed = false; anyPending = true }
                    if (c.status === 2 || c.status === 4) { allPassed = false; anyFail = true }
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
                    if (c.status === 2 || c.status === 4) anyFail = true
                    if (c.status === 0 || c.status === 3) anyPending = true
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
        return arr ? arr.length : 0
    }

    readonly property int criticalChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].status === 2 || arr[i].status === 4) n++
        }
        return n
    }

    readonly property int pendingChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
            if (arr[i].status === 0 || arr[i].status === 3) n++
        }
        return n
    }

    readonly property int passedChecks: {
        var n = 0
        var arr = _checks()
        for (var i = 0; i < arr.length; ++i) {
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
}
