// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

// Next to a setting changed for this game: what it would be otherwise, from
// the global settings (or without gfxtune), one click away.
QQC2.ToolButton {
    id: button

    required property GameSettingsEditor editor
    required property string setting
    // The global (or default) value, as the control shows it.
    property string value

    objectName: "backTo:" + setting
    visible: editor.changedSettings.includes(setting)
    icon.name: "edit-undo"
    text: editor.baseName + ": " + value
    font: Kirigami.Theme.smallFont
    display: QQC2.AbstractButton.TextBesideIcon
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    QQC2.ToolTip.text: editor.usesGlobal ? KI18n.i18n("Use the global setting for this game") : KI18n.i18n("Leave this setting to the game")
    onClicked: editor.useBase(setting)
}
