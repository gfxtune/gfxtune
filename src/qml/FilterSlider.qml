// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// One in-game filter: a slider in whole steps with its value on the right.
// Like the other controls it always shows `value`, and the mouse wheel
// scrolls the page instead of moving it.
RowLayout {
    id: row

    property int value
    property int from
    property int to
    // How the value reads; signed numbers by default.
    property var describe: v => (v > 0 ? "+" : "") + v
    signal picked(int value)

    QQC2.Slider {
        id: slider
        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
        wheelEnabled: false
        from: row.from
        to: row.to
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        value: row.value
        onMoved: {
            row.picked(Math.round(value));
            value = Qt.binding(() => row.value);
        }
    }
    QQC2.Label {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 4
        text: row.describe(row.value)
        font.features: { "tnum": 1 }
    }
}
