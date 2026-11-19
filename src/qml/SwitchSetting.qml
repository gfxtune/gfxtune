// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import org.kde.ki18n

// An on/off setting.
SettingRow {
    id: row

    property alias value: control.value
    property alias text: control.text
    property alias controlName: control.objectName
    property bool controlEnabled: true
    signal picked(bool value)

    baseValue: editor.baseValues[setting] ? KI18n.i18n("On") : KI18n.i18n("Off")

    SettingSwitch {
        id: control
        enabled: row.controlEnabled
        onPicked: value => row.picked(value)
    }
}
