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
//   _priorityListVisible (bool) — toggle for the priority fix list panel
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root
    anchors.fill: parent
    color: Colors.surface

    readonly property var catModels: ({
        0: CatModel0, 1: CatModel1, 2: CatModel2, 3: CatModel3,
        4: CatModel4, 5: CatModel5, 6: CatModel6, 7: CatModel7
    })

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

    // Enforced category display order: Power, GPS/Navigation, Communication, Airframe, Safety, Environment
    readonly property var catOrder: [1, 2, 3, 4, 5, 6, 0, 7]

    readonly property var typeNames: ["Auto", "Manual", "Action"]

    readonly property int _blockers: typeof PreflightChecklistModel !== "undefined" ? PreflightChecklistModel.blockingFailedCount : 0
    readonly property int _pend: typeof PreflightManager !== "undefined" ? PreflightManager.pendingChecks : 0
    readonly property int _pct: typeof PreflightChecklistModel !== "undefined" ? PreflightChecklistModel.completionPercent : 0
    readonly property int _total: typeof PreflightManager !== "undefined" ? PreflightManager.totalChecks : 0
    readonly property int _passed: typeof PreflightManager !== "undefined" ? PreflightManager.passedChecks : 0
    readonly property int _issueCount: typeof PreflightManager !== "undefined" ? PreflightManager.blockingCount : 0

    property var _runningChecks: ({})
    property var _expandedChecks: ({})
    property bool _priorityListVisible: false

    function catFailCount(catId) {
        var m = catModels[catId]
        if (!m) return 0
        var n = 0
        for (var i = 0; i < m.count; ++i) {
            if (m.get(i).status === 2) n++
        }
        return n
    }

    function catWarnCount(catId) {
        var m = catModels[catId]
        if (!m) return 0
        var n = 0
        for (var i = 0; i < m.count; ++i) {
            if (m.get(i).status === 3) n++
        }
        return n
    }

    function catPassCount(catId) {
        var m = catModels[catId]
        if (!m) return 0
        var n = 0
        for (var i = 0; i < m.count; ++i) {
            if (m.get(i).status === 1) n++
        }
        return n
    }

    function catSummary(catId) {
        var m = catModels[catId]
        if (!m || m.count === 0) return ""
        var f = catFailCount(catId), w = catWarnCount(catId), p = catPassCount(catId)
        if (f > 0) return f + " FAIL"
        if (w > 0) return w + " WARN"
        return p + "/" + m.count + " passed"
    }

    function timestamp() {
        var d = new Date()
        return d.getHours().toString().padStart(2, "0") + ":"
            + d.getMinutes().toString().padStart(2, "0") + ":"
            + d.getSeconds().toString().padStart(2, "0")
    }

    function nextBlocker(index) {
        if (typeof PreflightManager === "undefined") return ""
        var b = PreflightManager.blockingChecks
        if (!b || b.length <= index) return ""
        var item = b[index]
        return item && item.label ? item.label : ""
    }

    // ── Critical issues dialog ──
    Dialog {
        id: criticalIssuesDialog
        title: "Critical Issues"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width * 0.9)
        height: Math.min(420, parent.height * 0.8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Colors.surface; border.color: "#9333ea"; border.width: 2; radius: 12 }
        padding: 0

        header: Rectangle {
            height: 48
            color: "#2D1B4E"
            radius: 12
            // Flatten bottom corners
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: parent.color }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 16
                Text { text: "\u26A0 Critical / Blocking Issues"; font.pixelSize: 15; font.bold: true; color: "#F8BBD0"; Layout.fillWidth: true }
                Text { text: _issueCount + " issue" + (_issueCount !== 1 ? "s" : ""); font.pixelSize: 12; color: "#CE93D8" }
            }
        }

        contentItem: Flickable {
            clip: true
            contentHeight: issueCol.implicitHeight + 16
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: issueCol
                width: parent.width
                spacing: 6
                anchors.margins: 12

                Repeater {
                    model: typeof PreflightManager !== "undefined" ? PreflightManager.blockingChecks : []

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property var chk: modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Layout.topMargin: index === 0 ? 8 : 0
                        height: 60
                        radius: 8
                        color: Colors.errorDim
                        border.color: Colors.error; border.width: 1

                        RowLayout {
                            anchors.fill: parent; anchors.margins: 10
                            spacing: 10

                            Rectangle {
                                width: 28; height: 28; radius: 14
                                color: Colors.error
                                Text { anchors.centerIn: parent; text: (index + 1).toString(); font.pixelSize: 11; font.bold: true; color: "#fff" }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text { text: chk ? chk.label : ""; font.pixelSize: 13; font.bold: true; color: Colors.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: chk ? chk.message : ""; font.pixelSize: 11; color: Colors.textSecondary; elide: Text.ElideRight; maximumLineCount: 1; Layout.fillWidth: true }
                            }

                            Rectangle {
                                width: 72; height: 26; radius: 6
                                color: "#9333ea"
                                Text { anchors.centerIn: parent; text: "Go to Check"; font.pixelSize: 9; font.bold: true; color: "#fff" }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        criticalIssuesDialog.close()
                                        // Scroll to category containing this check
                                        if (chk) {
                                            var catId = chk.checkCategory
                                            for (var i = 0; i < catOrder.length; ++i) {
                                                if (catOrder[i] === catId) {
                                                    var yPos = i * 180
                                                    checklistFlickable.contentY = Math.min(yPos, checklistFlickable.contentHeight - checklistFlickable.height)
                                                    break
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Empty state
                Text {
                    visible: _issueCount === 0
                    text: "\u2713 No critical issues"
                    font.pixelSize: 14; color: Colors.success
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 20
                }
            }
        }

        footer: Rectangle {
            height: 44
            color: Colors.surfaceLight
            radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: parent.color }
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                Item { Layout.fillWidth: true }
                Rectangle {
                    width: 80; height: 28; radius: 6
                    color: "#9333ea"
                    Text { anchors.centerIn: parent; text: "Close"; font.pixelSize: 12; font.bold: true; color: "#fff" }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: criticalIssuesDialog.close() }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Top-level summary box (pink/purple themed) ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            Layout.topMargin: Config.spacingSmall
            radius: 10
            visible: _total > 0

            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#2D1B4E" }
                GradientStop { position: 0.5; color: "#4A1942" }
                GradientStop { position: 1.0; color: "#6B1D5E" }
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
                        font.pixelSize: 15; font.bold: true; color: "#F8BBD0"
                    }
                    Text {
                        text: _issueCount > 0 ? _issueCount + " critical issue" + (_issueCount !== 1 ? "s" : "") : "No critical issues"
                        font.pixelSize: 12
                        color: _issueCount > 0 ? "#EF9A9A" : "#A5D6A7"
                    }
                }

                Item { Layout.fillWidth: true }

                // Circular progress indicator
                Rectangle {
                    width: 36; height: 36; radius: 18
                    color: "transparent"
                    border.color: "#CE93D8"; border.width: 2

                    Text {
                        anchors.centerIn: parent
                        text: _pct + "%"
                        font.pixelSize: 10; font.bold: true; color: "#CE93D8"
                    }
                }

                // Show button
                Rectangle {
                    width: 64; height: 30; radius: 8
                    color: _issueCount > 0 ? "#E91E63" : "#9333ea"
                    opacity: showBtnMa.containsMouse ? 0.9 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: "Show"
                        font.pixelSize: 12; font.bold: true; color: "#fff"
                    }

                    MouseArea {
                        id: showBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: criticalIssuesDialog.open()
                    }
                }
            }
        }

        // Blocking banner with integrated issue list toggle
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 48 : 0
            Layout.topMargin: 6
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            color: Colors.errorDim
            radius: Config.radiusSmall
            visible: _blockers > 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                spacing: Config.spacingMedium

                Text {
                    text: "\u26A0 Arming blocked: " + _blockers + " critical " + (_blockers === 1 ? "item must" : "items must") + " be fixed"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.error
                    Layout.preferredWidth: Math.max(220, parent.width * 0.35)
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        text: nextBlocker(0).length > 0 ? "First: Fix " + nextBlocker(0) : ""
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.error
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

                Text {
                    text: _issueCount + " issue(s)"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.error
                    font.bold: true
                    visible: _issueCount > 0
                }

                Rectangle {
                    Layout.preferredHeight: 22
                    Layout.preferredWidth: 52
                    radius: 3
                    color: Colors.error
                    visible: _issueCount > 0
                    Text {
                        anchors.centerIn: parent
                        text: root._priorityListVisible ? "HIDE" : "SHOW"
                        font.pixelSize: 9
                        font.bold: true
                        color: Colors.background
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root._priorityListVisible = !root._priorityListVisible
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // Priority fix list (collapsible)
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root._priorityListVisible && _issueCount > 0 ? priorityPanel.implicitHeight : 0
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            clip: true
            visible: root._priorityListVisible && _issueCount > 0
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 200 } }

            PriorityFixPanel {
                id: priorityPanel
                width: parent.width
            }
        }

        // Ready callout
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            Layout.topMargin: 6
            Layout.leftMargin: Config.spacingMedium
            Layout.rightMargin: Config.spacingMedium
            color: Colors.successDim
            radius: Config.radiusSmall
            visible: _blockers === 0 && _total > 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                spacing: Config.spacingSmall
                Text {
                    text: _pend > 0 ? "\u2713 All critical checks passed. " + _pend + " item(s) pending."
                         : _pct === 100 ? "\u2713 Ready to arm. " + _total + "/" + _total + " checks passed."
                         : "\u2713 " + _passed + "/" + _total + " checks passed"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.success
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    text: _pend > 0 ? "Review warnings before arming." : ""
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.warning
                    visible: _pend > 0
                }
            }
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
                    model: catOrder

                    delegate: Column {
                        id: section
                        readonly property int catId: modelData
                        readonly property var info: catInfo[catId]
                        readonly property var modelObj: catModels[catId]
                        readonly property int catCount: modelObj ? modelObj.count : 0
                        readonly property int failCount: catFailCount(catId)
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
                                    text: catSummary(catId)
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: hasFail ? Colors.error
                                         : catWarnCount(catId) > 0 ? Colors.checkWarn
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

                            readonly property int gridColumns: Math.max(2, Math.min(5, Math.floor((width + 6) / 220)))
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

                                        width: gridHost.cellWidth
                                        height: expanded ? expandedCol.implicitHeight + 10 : collapsedH
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

                                        property bool expanded: root._expandedChecks[checkId] === true
                                        readonly property int collapsedH: isManual || isAction ? 72 : 56
                                        property string _checkTime: ""

                                    ToolTip {
                                        visible: tooltipMa.containsMouse
                                        text: "ID: " + checkId + "\nType: " + typeNames[type]
                                            + "\nCategory: " + catInfo[category].label
                                            + "\nMandatory: " + (mandatory ? "Yes" : "No")
                                            + "\nStatus: " + (status === 1 ? "Passed" : status === 2 ? "Failed" : status === 3 ? "Warning" : "Pending")
                                        delay: 600
                                    }

                                    MouseArea {
                                        id: tooltipMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            root._expandedChecks[checkId] = !root._expandedChecks[checkId]
                                            root._expandedChecks = root._expandedChecks
                                        }
                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    ColumnLayout {
                                        id: expandedCol
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
                                                font.pixelSize: 14
                                                color: status === 1 ? Colors.success
                                                     : status === 2 ? Colors.error
                                                     : status === 3 ? Colors.checkWarn
                                                     : Colors.textDisabled
                                            }

                                            Text {
                                                text: label
                                                font.pixelSize: Config.fontSizeSmall
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
                                                Layout.preferredHeight: 18
                                                Layout.preferredWidth: 40
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "PASS"
                                                    color: Colors.background
                                                    font.pixelSize: 9
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle {
                                                visible: status === 2
                                                color: Colors.error
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: 18
                                                Layout.preferredWidth: 40
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "FAIL"
                                                    color: Colors.background
                                                    font.pixelSize: 9
                                                    font.bold: true
                                                }
                                            }
                                            Rectangle {
                                                visible: status === 3
                                                color: Colors.checkWarn
                                                radius: Config.radiusSmall
                                                Layout.preferredHeight: 18
                                                Layout.preferredWidth: 40
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "WARN"
                                                    color: Colors.background
                                                    font.pixelSize: 9
                                                    font.bold: true
                                                }
                                            }

                                            Text {
                                                text: checkItem.expanded ? "\u25B2" : "\u25BC"
                                                font.pixelSize: 8
                                                color: Colors.textSecondary
                                            }
                                        }

                                        // Row 2: status message
                                        Text {
                                            Layout.fillWidth: true
                                            visible: message.length > 0 && !checkItem.expanded && !isManual
                                            text: message
                                            font.pixelSize: Config.fontSizeSmall
                                            color: Colors.textSecondary
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                            Layout.leftMargin: 20
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
                                                    font.pixelSize: Config.fontSizeSmall
                                                    color: Colors.textSecondary
                                                    elide: Text.ElideRight
                                                    maximumLineCount: 1
                                                    Layout.leftMargin: 20
                                                }

                                                Rectangle {
                                                    id: manualBtn
                                                    visible: isManual
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 28
                                                    Layout.leftMargin: 12
                                                    Layout.rightMargin: 12
                                                    radius: Config.radiusSmall
                                                    color: status === 1 ? Colors.success : Colors.surface
                                                    border.color: status === 1 ? Colors.success : Colors.border
                                                    border.width: 1

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: actionButtonText.length > 0 ? actionButtonText : "Mark as Checked"
                                                        font.pixelSize: Config.fontSizeSmall
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
                                                                checkItem._checkTime = timestamp()
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
                                                    actionLabel: actionButtonText.length > 0 ? actionButtonText : "Run"
                                                    running: root._runningChecks[checkId] === true
                                                    Layout.fillWidth: true
                                                    Layout.leftMargin: 8
                                                    Layout.rightMargin: 8

                                                    onActionTriggered: function(cid) {
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
                                                color: "#2D1B4E"
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
                                                        Text { text: "Current:"; font.pixelSize: Config.fontSizeSmall; color: "#FFFFFF" }
                                                        Text {
                                                            text: currentValueString
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: "#F8BBD0"
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                        }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: threshold.length > 0
                                                        Text { text: "Required:"; font.pixelSize: Config.fontSizeSmall; color: "#FFFFFF" }
                                                        Text {
                                                            text: threshold
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: "#F8BBD0"
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                        }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: rationale.length > 0
                                                        Text { text: "Why:"; font.pixelSize: Config.fontSizeSmall; color: "#FFFFFF" }
                                                        Text {
                                                            text: rationale
                                                            font.pixelSize: Config.fontSizeSmall
                                                            color: "#F8BBD0"
                                                            Layout.fillWidth: true
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 4
                                                        }
                                                    }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        visible: fixSteps && fixSteps.length > 0
                                                        Text { text: "How to fix:"; font.pixelSize: Config.fontSizeSmall; color: "#FFFFFF" }
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
                                                                    color: "#E91E63"
                                                                }
                                                                Text {
                                                                    text: modelData
                                                                    font.pixelSize: Config.fontSizeSmall
                                                                    color: "#F8BBD0"
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
            }
        }

        // Fixed footer
        ArmGatePanel {
            Layout.fillWidth: true
            Layout.preferredHeight: Config.kFooterHeight
        }
    }
}
