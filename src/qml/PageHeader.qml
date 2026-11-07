// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend

// The top of the settings page: what All Games means, or the game's cover,
// how it runs, and whether it follows the global settings.
ColumnLayout {
    id: header

    required property GameSettingsEditor editor

    spacing: Kirigami.Units.smallSpacing

    ColumnLayout {
        objectName: "globalHeader"
        Layout.fillWidth: true
        visible: header.editor.global
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 1
            text: "All Games"
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "These settings apply to every game. What you change on a game's own page wins over them for that game, and a game can stop using them there."
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: !header.editor.global
        spacing: Kirigami.Units.gridUnit

        Cover {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 5
            Layout.preferredHeight: Kirigami.Units.gridUnit * 7.5
            source: header.editor.cover
            name: header.editor.gameName
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                text: header.editor.gameName
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                text: header.editor.runsAs
            }
            QQC2.Label {
                text: header.editor.graphics
                opacity: 0.7
            }
            QQC2.CheckBox {
                objectName: "usesGlobal"
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: "Use the global settings"
                checked: header.editor.usesGlobal
                onToggled: {
                    header.editor.usesGlobal = checked;
                    checked = Qt.binding(() => header.editor.usesGlobal);
                }
            }
            Note {
                readonly property int changes: header.editor.changedSettings.length
                text: !header.editor.usesGlobal
                    ? "This game ignores the settings for All Games: gfxtune leaves it alone apart from its own changes."
                    : changes === 0 ? "Every setting comes from All Games."
                    : changes === 1 ? "1 setting is changed for this game; the rest come from All Games."
                    : changes + " settings are changed for this game; the rest come from All Games."
            }
        }
    }
}
