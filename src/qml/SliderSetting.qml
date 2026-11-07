// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import Gfxtune.Backend

// An in-game filter's slider, with the button that puts it back to the
// global value when the game changed it.
RowLayout {
    id: row

    required property GameSettingsEditor editor
    // The setting's id, as GameSettingsEditor names it.
    required property string setting
    property alias value: slider.value
    property alias from: slider.from
    property alias to: slider.to
    property alias describe: slider.describe
    property alias controlName: slider.objectName
    property bool controlEnabled: true
    signal picked(int value)

    FilterSlider {
        id: slider
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
    BackToBase {
        editor: row.editor
        setting: row.setting
        value: slider.describe(row.editor.baseValues[row.setting])
    }
}
