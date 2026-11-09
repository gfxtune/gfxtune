// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

// The game's measured sessions, newest first: the frame rates and the card's
// readings, the newest few unless all are asked for. A table while its
// columns fit the page, otherwise a list with a few lines per session.
ColumnLayout {
    id: history

    required property GameSettingsEditor editor
    // The page's width. The column's own width cannot tell: a layout is
    // never narrower than the fixed-width table inside it.
    required property real room

    readonly property var sessions: editor.sessions
    readonly property int collapsedCount: 5
    property bool expanded: false
    readonly property int rowCount: expanded ? sessions.length : Math.min(sessions.length, collapsedCount)
    // The columns, with the key of each in a session row.
    readonly property var columns: [
        {title: KI18n.i18n("Date"), key: "date"},
        {title: KI18n.i18n("Played"), key: "played"},
        {title: KI18n.i18n("Average FPS"), key: "averageFps"},
        {title: KI18n.i18n("1% low"), key: "low1"},
        {title: KI18n.i18n("0.1% low"), key: "low01"},
        {title: KI18n.i18n("Temperature"), key: "temperature"},
        {title: KI18n.i18n("Hotspot (max)"), key: "hotspot"},
        {title: KI18n.i18n("Power"), key: "power"}
    ]
    // The table while it fits. The width it needs is measured while it
    // shows, so the list can tell when there is room for it again.
    property bool wide: true
    property real wideWidth: 0

    function fit() {
        if (room <= 0) {
            return;
        }
        if (wide) {
            wideWidth = table.implicitWidth;
            wide = wideWidth <= room;
        } else {
            wide = room >= wideWidth;
        }
    }

    // The readings that were taken, for the list.
    function joined(parts) {
        return parts.filter(part => !part.endsWith("—")).join(" · ");
    }

    onRoomChanged: fit()
    spacing: Kirigami.Units.largeSpacing

    QQC2.Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 24
        visible: history.sessions.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        opacity: 0.7
        text: KI18n.i18n("No sessions yet. Each time the game runs for 20 seconds or more with performance measuring on, it appears here.")
    }

    GridLayout {
        id: table
        objectName: "sessionTable"
        Layout.alignment: Qt.AlignHCenter
        visible: history.wide && history.sessions.length > 0
        columns: history.columns.length
        columnSpacing: Kirigami.Units.gridUnit
        rowSpacing: Kirigami.Units.smallSpacing
        onImplicitWidthChanged: history.fit()

        // The header row, then one row per session.
        Repeater {
            model: (history.rowCount + 1) * history.columns.length
            delegate: QQC2.Label {
                required property int index
                readonly property int column: index % history.columns.length
                readonly property int row: Math.floor(index / history.columns.length) - 1
                readonly property var session: row >= 0 && row < history.sessions.length ? history.sessions[row] : null

                Layout.alignment: column < 2 ? Qt.AlignLeft : Qt.AlignRight
                opacity: row < 0 ? 0.7 : 1
                font.italic: session !== null && session.running
                font.features: { "tnum": 1 }
                text: row < 0 ? history.columns[column].title
                    : session !== null ? session[history.columns[column].key] : ""
            }
        }
    }

    ColumnLayout {
        objectName: "sessionList"
        Layout.fillWidth: true
        visible: !history.wide && history.sessions.length > 0
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: history.rowCount
            delegate: ColumnLayout {
                id: entry

                required property int index
                readonly property var session: index < history.sessions.length ? history.sessions[index] : null

                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.italic: entry.session !== null && entry.session.running
                    text: entry.session !== null ? entry.session.date + " · " + entry.session.played : ""
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.features: { "tnum": 1 }
                    text: entry.session !== null
                        ? KI18n.i18n("%1 FPS average · 1% low %2 · 0.1% low %3", entry.session.averageFps, entry.session.low1, entry.session.low01)
                        : ""
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    opacity: 0.7
                    font.features: { "tnum": 1 }
                    text: entry.session !== null
                        ? history.joined([entry.session.temperature, KI18n.i18n("hotspot %1", entry.session.hotspot), entry.session.power])
                        : ""
                }
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        visible: history.sessions.length > 0

        QQC2.Button {
            visible: history.sessions.length > history.collapsedCount
            text: history.expanded ? KI18n.i18n("Show Fewer") : KI18n.i18n("Show All %1", history.sessions.length)
            icon.name: history.expanded ? "collapse" : "expand"
            flat: true
            onClicked: history.expanded = !history.expanded
        }
        QQC2.Button {
            text: KI18n.i18n("Clear History")
            icon.name: "edit-clear-history"
            flat: true
            onClicked: clearDialog.open()
        }
    }

    Kirigami.PromptDialog {
        id: clearDialog
        title: KI18n.i18n("Clear this game's performance history?")
        subtitle: KI18n.i18n("The sessions listed here are removed. A session still running stays, and new ones are measured as before.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: history.editor.clearSessions()
    }
}
