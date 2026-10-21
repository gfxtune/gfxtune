// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Controls as QQC2

// A switch that always shows `value`, including after the user flips it and
// when another game is selected. Flipping it reports through `picked`.
QQC2.Switch {
    id: control

    property bool value
    signal picked(bool value)

    checked: value
    onToggled: {
        picked(checked);
        checked = Qt.binding(() => control.value);
    }
}
