// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

ColumnLayout {
    id: pane

    required property GameLibrary library
    required property GameSettingsEditor editor

    spacing: 0

    // Keeps the highlighted row on the selected game when the list changes
    // under it (a rescan, or typing in the search field).
    function syncSelection() {
        list.currentIndex = editor.global ? -1 : library.games.rowOf(editor.appId);
    }

    Connections {
        target: pane.library.games
        function onModelReset() { pane.syncSelection(); }
        function onRowsInserted() { pane.syncSelection(); }
        function onRowsRemoved() { pane.syncSelection(); }
        function onLayoutChanged() { pane.syncSelection(); }
    }
    Connections {
        target: pane.editor
        function onGameChanged() { pane.syncSelection(); }
    }

    Kirigami.SearchField {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        placeholderText: KI18n.i18n("Search games…")
        onTextChanged: pane.library.games.filterText = text
    }

    // The global settings, above every game.
    QQC2.ItemDelegate {
        objectName: "allGames"
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.smallSpacing
        Layout.rightMargin: Kirigami.Units.smallSpacing
        highlighted: pane.editor.global
        onClicked: pane.editor.selectGlobal()

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.Theme.alternateBackgroundColor

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Kirigami.Units.iconSizes.smallMedium
                    height: width
                    source: "globe"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    Layout.fillWidth: true
                    text: KI18n.i18n("All Games")
                    elide: Text.ElideRight
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: KI18n.i18np("Global settings · %2 of %1 game uses them", "Global settings · %2 of %1 games use them",
                                      pane.library.gameCount, pane.library.gamesUsingGlobal)
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    elide: Text.ElideRight
                }
            }
        }
    }
    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
    }

    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: list

            model: pane.library.games
            clip: true
            keyNavigationEnabled: true
            currentIndex: -1
            onCurrentIndexChanged: {
                if (currentIndex >= 0) {
                    pane.editor.appId = pane.library.games.appIdAt(currentIndex);
                }
            }

            delegate: QQC2.ItemDelegate {
                id: row

                required property int index
                required property int appId
                required property string name
                required property string detail
                required property url cover
                required property string settingsNote
                required property bool running

                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: {
                    ListView.view.forceActiveFocus();
                    ListView.view.currentIndex = index;
                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    Cover {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        source: row.cover
                        name: row.name
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.name
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.settingsNote.length > 0 ? row.detail + " · " + row.settingsNote : row.detail
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            elide: Text.ElideRight
                        }
                    }

                    QQC2.Label {
                        visible: row.running
                        text: KI18n.i18n("Running")
                        font: Kirigami.Theme.smallFont
                        // Readable on the selection highlight too.
                        color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.positiveTextColor
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 2
                visible: list.count === 0 && pane.library.games.filterText.length > 0
                text: KI18n.i18n("No game matches")
            }
        }
    }
}
