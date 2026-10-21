// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Gfxtune.Backend

Kirigami.ApplicationWindow {
    id: root

    required property GameLibrary library
    required property GameSettingsEditor editor

    width: Kirigami.Units.gridUnit * 64
    height: Kirigami.Units.gridUnit * 40
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 25
    title: editor.hasGame ? editor.gameName + " — gfxtune" : "gfxtune"

    // Select the first game once the list is there.
    Connections {
        target: root.library
        function onStateChanged() {
            if (root.editor.appId === 0 && root.library.gameCount > 0) {
                root.editor.appId = root.library.games.appIdAt(0);
            }
        }
    }

    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: root.editor.hasGame ? root.editor.gameName : "gfxtune"

        actions: [
            Kirigami.Action {
                text: "Refresh"
                icon.name: "view-refresh"
                tooltip: "Read the Steam library again"
                enabled: !root.library.scanning
                onTriggered: root.library.refresh()
            },
            Kirigami.Action {
                text: "Remove All gfxtune Settings…"
                icon.name: "edit-clear-all"
                displayHint: Kirigami.DisplayHint.AlwaysHide
                onTriggered: removeAllDialog.open()
            },
            Kirigami.Action {
                text: "About gfxtune"
                icon.name: "help-about"
                displayHint: Kirigami.DisplayHint.AlwaysHide
                onTriggered: aboutDialog.open()
            }
        ]

        Kirigami.LoadingPlaceholder {
            anchors.centerIn: parent
            visible: !root.library.loaded
            text: "Reading your Steam library…"
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 8
            visible: root.library.loaded && !root.library.steamFound
            icon.name: "dialog-information"
            text: "Steam was not found"
            explanation: "gfxtune reads the games of the Steam client installed from your system's packages. Steam from Flathub is not supported yet."
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0
            visible: root.library.loaded && root.library.steamFound

            GameListPane {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 17
                Layout.fillHeight: true
                library: root.library
                editor: root.editor
            }

            Kirigami.Separator {
                Layout.fillHeight: true
            }

            GameSettingsPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.editor.hasGame
                editor: root.editor
            }

            Kirigami.PlaceholderMessage {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                Layout.margins: Kirigami.Units.gridUnit * 4
                visible: !root.editor.hasGame
                text: root.library.gameCount > 0 ? "Choose a game" : "No installed games found"
                explanation: root.library.gameCount > 0 ? "" : "Games installed with Steam appear here on their own."
            }
        }
    }

    Kirigami.PromptDialog {
        id: removeAllDialog
        title: "Remove all gfxtune settings?"
        subtitle: "Every game goes back to its own settings. Steam and your games are not changed."
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.editor.removeAllSettings()
    }

    Kirigami.PromptDialog {
        id: aboutDialog
        title: "gfxtune " + Qt.application.version
        subtitle: "Per-game graphics settings and live GPU readings for Linux.\nLicensed under the GNU GPL, version 3 or later."
        standardButtons: Kirigami.Dialog.Close
    }
}
