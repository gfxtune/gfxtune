// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick

// An in-game filter's slider.
SettingRow {
    id: row

    property alias value: slider.value
    property alias from: slider.from
    property alias to: slider.to
    property alias describe: slider.describe
    property alias controlName: slider.objectName
    property bool controlEnabled: true
    signal picked(int value)

    baseValue: slider.describe(editor.baseValues[setting])

    FilterSlider {
        id: slider
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
}
