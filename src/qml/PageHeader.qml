// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

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
            text: KI18n.i18n("All Games")
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: KI18n.i18n("These settings apply to every game. What you change on a game's own page wins over them for that game, and a game can stop using them there.")
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
                Layout.fillWidth: true
                text: header.editor.runsAs
                wrapMode: Text.Wrap
            }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                // Wraps when the page is narrow; the button stays right after it.
                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: header.editor.graphics
                    wrapMode: Text.Wrap
                    opacity: 0.7
                }
                QQC2.Button {
                    objectName: "checkGraphicsAgain"
                    visible: header.editor.graphicsSeen
                    flat: true
                    icon.name: "view-refresh"
                    text: KI18n.i18n("Check Again")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    QQC2.ToolTip.text: KI18n.i18n("If the game only showed a launcher, gfxtune looks again the next time it starts.")
                    onClicked: header.editor.checkGraphicsAgain()
                }
            }
            QQC2.CheckBox {
                objectName: "usesGlobal"
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: KI18n.i18n("Use the global settings")
                checked: header.editor.usesGlobal
                onToggled: {
                    header.editor.usesGlobal = checked;
                    checked = Qt.binding(() => header.editor.usesGlobal);
                }
            }
            Note {
                readonly property int changes: header.editor.changedSettings.length
                text: !header.editor.usesGlobal
                    ? KI18n.i18n("This game ignores the settings for All Games: gfxtune leaves it alone apart from its own changes.")
                    : changes === 0 ? KI18n.i18n("Every setting comes from All Games.")
                    : KI18n.i18np("1 setting is changed for this game; the rest come from All Games.",
                                  "%1 settings are changed for this game; the rest come from All Games.", changes)
            }
        }
    }
}
