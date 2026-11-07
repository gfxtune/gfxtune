// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import Gfxtune.Backend

// An on/off setting, with the button that puts it back to the global value
// when the game changed it.
RowLayout {
    id: row

    required property GameSettingsEditor editor
    // The setting's id, as GameSettingsEditor names it.
    required property string setting
    property alias value: control.value
    property alias text: control.text
    property alias controlName: control.objectName
    property bool controlEnabled: true
    signal picked(bool value)

    SettingSwitch {
        id: control
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
    BackToBase {
        editor: row.editor
        setting: row.setting
        value: row.editor.baseValues[row.setting] ? "On" : "Off"
    }
}
