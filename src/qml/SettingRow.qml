// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import Gfxtune.Backend

// A setting's controls with the button that puts it back to the global
// value when the game changed it: beside them, or under them when the page
// is too narrow for both (a small window, a long translation).
GridLayout {
    id: row

    required property GameSettingsEditor editor
    // The setting's id, as GameSettingsEditor names it.
    required property string setting
    // The global (or default) value, as the controls show it.
    property string baseValue
    default property alias content: controls.data

    readonly property real besideWidth: controls.implicitWidth + (back.visible ? columnSpacing + back.implicitWidth : 0)

    columns: width >= besideWidth ? 2 : 1
    // Set by what the row holds, not by the choice above: the form's width
    // never depends on it, and a stacked row can widen again.
    Layout.minimumWidth: Math.max(controls.implicitWidth, back.visible ? back.implicitWidth : 0)
    Layout.preferredWidth: besideWidth
    Layout.maximumWidth: besideWidth

    RowLayout {
        id: controls
        Layout.fillWidth: false
    }
    BackToBase {
        id: back
        editor: row.editor
        setting: row.setting
        value: row.baseValue
    }
}
