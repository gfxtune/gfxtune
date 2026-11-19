// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick

// A setting chosen from a list of {text, value} entries.
SettingRow {
    id: row

    property alias model: box.model
    property alias value: box.value
    property alias controlName: box.objectName
    property bool controlEnabled: true
    signal picked(int value)

    function textOf(value) {
        const found = (box.model ?? []).find(choice => choice.value === value);
        return found ? found.text : String(value);
    }

    baseValue: textOf(editor.baseValues[setting])

    ValueComboBox {
        id: box
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
}
