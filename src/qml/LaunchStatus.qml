// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

// Where the game stands with gfxtune-run in its Steam launch options, and
// the line to paste while Steam is open.
ColumnLayout {
    id: status

    required property GameSettingsEditor editor
    // The numbers follow GameLibrary::LaunchState.
    readonly property int launch: editor.launchState
    readonly property bool showsLine: launch === 2 && editor.steamRunning

    spacing: Kirigami.Units.largeSpacing

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: status.launch === 1 ? Kirigami.MessageType.Positive
            : status.launch === 4 ? Kirigami.MessageType.Error
            : Kirigami.MessageType.Information
        text: {
            switch (status.launch) {
            case 1:
                return KI18n.i18n("gfxtune is in this game's Steam launch options. Changes apply the next time the game starts.");
            case 2:
                return status.editor.steamRunning
                    ? KI18n.i18n("Steam is open, so gfxtune adds itself to this game's launch options once Steam closes. To use these settings sooner, paste this line into the game's launch options in Steam:")
                    : KI18n.i18n("Adding gfxtune to this game's Steam launch options…");
            case 3:
                return status.editor.steamRunning
                    ? KI18n.i18n("gfxtune takes itself out of this game's launch options once Steam closes. Until then the game starts with its own settings.")
                    : KI18n.i18n("Taking gfxtune out of this game's Steam launch options…");
            case 4:
                return KI18n.i18n("Steam's launch options could not be updated: %1", status.editor.launchProblem);
            case 5:
                return KI18n.i18n("gfxtune stays out of this game by itself: it ships anti-cheat, and All Games has in-game filters or measuring on, which run inside games. To add gfxtune, accept the risk below or change a setting for this game, or switch those off on All Games.");
            default:
                return KI18n.i18n("gfxtune has nothing to do in this game, so Steam starts it as usual. Change something below and gfxtune adds itself to the game's Steam launch options.");
            }
        }
        // Only when it shows: Kirigami counts a hidden action too, and then
        // moves it below and back as the text wraps.
        actions: status.showsLine ? [copyLineAction] : []

        Kirigami.Action {
            id: copyLineAction
            text: KI18n.i18n("Copy Line")
            icon.name: "edit-copy"
            onTriggered: status.editor.copyLaunchLine()
        }
    }

    QQC2.TextField {
        objectName: "launchLine"
        Layout.fillWidth: true
        visible: status.showsLine
        readOnly: true
        text: status.editor.launchLine
        font.family: "monospace"
        // A line longer than the box shows its start, not its end.
        onTextChanged: cursorPosition = 0
        Component.onCompleted: cursorPosition = 0
    }
}
