// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Controls as QQC2

// A list of {text, value} choices that shows the entry for `value` and
// reports the user's pick through `picked`. The mouse wheel scrolls the page
// instead of changing the choice, so scrolling never edits a setting.
QQC2.ComboBox {
    id: control

    property int value
    signal picked(int value)

    wheelEnabled: false
    textRole: "text"
    valueRole: "value"

    onActivated: picked(currentValue)
    onValueChanged: currentIndex = indexOfValue(value)
    onModelChanged: currentIndex = indexOfValue(value)
    Component.onCompleted: currentIndex = indexOfValue(value)
}
