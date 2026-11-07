// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import Gfxtune.Backend

// A setting chosen from a list of {text, value} entries, with the button
// that puts it back to the global value when the game changed it.
RowLayout {
    id: row

    required property GameSettingsEditor editor
    // The setting's id, as GameSettingsEditor names it.
    required property string setting
    property alias model: box.model
    property alias value: box.value
    property alias controlName: box.objectName
    property bool controlEnabled: true
    signal picked(int value)

    function textOf(value) {
        const found = (box.model ?? []).find(choice => choice.value === value);
        return found ? found.text : String(value);
    }

    ValueComboBox {
        id: box
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
    BackToBase {
        editor: row.editor
        setting: row.setting
        value: row.textOf(row.editor.baseValues[row.setting])
    }
}
