// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

Kirigami.ApplicationWindow {
    id: root

    required property GameLibrary library
    required property GameSettingsEditor editor
    required property GpuMonitor gpu

    width: Kirigami.Units.gridUnit * 76
    height: Kirigami.Units.gridUnit * 42
    minimumWidth: Kirigami.Units.gridUnit * 56
    minimumHeight: Kirigami.Units.gridUnit * 25
    title: editor.global ? KI18n.i18n("All Games — gfxtune") : editor.hasGame ? KI18n.i18nc("window title", "%1 — gfxtune", editor.gameName) : "gfxtune"

    // Read the card only while the window can be seen, also when it sits on
    // a second monitor during a game; not while it is minimised.
    Binding {
        target: root.gpu
        property: "active"
        value: root.visible && root.visibility !== Window.Minimized
    }

    // Select the first game once the list is there.
    Connections {
        target: root.library
        function onStateChanged() {
            if (root.editor.appId === 0 && !root.editor.global && root.library.gameCount > 0) {
                root.editor.appId = root.library.games.appIdAt(0);
            }
        }
    }

    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: root.editor.global ? KI18n.i18n("All Games") : root.editor.hasGame ? root.editor.gameName : "gfxtune"

        actions: [
            Kirigami.Action {
                text: KI18n.i18n("Refresh")
                icon.name: "view-refresh"
                tooltip: KI18n.i18n("Read the Steam library again")
                enabled: !root.library.scanning
                onTriggered: root.library.refresh()
            },
            Kirigami.Action {
                text: KI18n.i18n("Remove All gfxtune Settings…")
                icon.name: "edit-clear-all"
                displayHint: Kirigami.DisplayHint.AlwaysHide
                onTriggered: removeAllDialog.open()
            },
            Kirigami.Action {
                text: KI18n.i18n("About gfxtune")
                icon.name: "help-about"
                displayHint: Kirigami.DisplayHint.AlwaysHide
                onTriggered: aboutDialog.open()
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Games and their settings.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Kirigami.LoadingPlaceholder {
                    anchors.centerIn: parent
                    visible: !root.library.loaded
                    text: KI18n.i18n("Reading your Steam library…")
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: root.library.loaded && !root.library.steamFound
                    icon.name: "dialog-information"
                    text: KI18n.i18n("Steam was not found")
                    explanation: KI18n.i18n("gfxtune reads the games of the Steam client installed from your system's packages. Steam from Flathub is not supported yet.")
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
                        objectName: "gameSettingsPane"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.editor.hasGame || root.editor.global
                        editor: root.editor
                    }

                    Kirigami.PlaceholderMessage {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignCenter
                        Layout.margins: Kirigami.Units.gridUnit * 4
                        visible: !root.editor.hasGame && !root.editor.global
                        text: root.library.gameCount > 0 ? KI18n.i18n("Choose a game") : KI18n.i18n("No installed games found")
                        explanation: root.library.gameCount > 0 ? "" : KI18n.i18n("Games installed with Steam appear here on their own.")
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillHeight: true
            }

            GpuPanel {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                Layout.fillHeight: true
                Layout.margins: Kirigami.Units.largeSpacing * 2
                monitor: root.gpu
                library: root.library
            }
        }
    }

    Kirigami.PromptDialog {
        id: removeAllDialog
        title: KI18n.i18n("Remove all gfxtune settings?")
        subtitle: KI18n.i18n("The global settings and every game's own go back to gfxtune's defaults, which measure performance in every game. Your games and other Steam settings are not changed.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.editor.removeAllSettings()
    }

    Kirigami.PromptDialog {
        id: aboutDialog
        iconName: "gfxtune"
        title: "gfxtune " + Qt.application.version
        subtitle: KI18n.i18n("Graphics and game tuning for Steam games on CachyOS.\nLicensed under the GNU GPL, version 3 or later.")
        standardButtons: Kirigami.Dialog.Close
    }
}
