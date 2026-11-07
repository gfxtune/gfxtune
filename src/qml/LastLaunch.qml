// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend

// What gfxtune-run applied when the game last started, and what the layer
// reported from inside it.
Kirigami.FormLayout {
    id: form

    required property GameSettingsEditor editor

    component Line: QQC2.Label {
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 24
        visible: text.length > 0
        wrapMode: Text.Wrap
    }

    Kirigami.Separator {
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: "Last launch"
    }
    QQC2.Label {
        visible: !form.editor.hasLastLaunch
        text: "Not started through gfxtune yet."
        opacity: 0.7
    }
    QQC2.Label {
        Kirigami.FormData.label: "Started:"
        visible: form.editor.hasLastLaunch
        text: form.editor.lastLaunchTime
    }
    Line {
        Kirigami.FormData.label: "Applied:"
        visible: form.editor.hasLastLaunch
        text: form.editor.lastLaunchApplied.length > 0 ? form.editor.lastLaunchApplied : "Nothing"
    }
    Line {
        Kirigami.FormData.label: "Left to Steam's options:"
        text: form.editor.lastLaunchKept
    }
    Line {
        Kirigami.FormData.label: "Not applied:"
        text: form.editor.lastLaunchNotApplied
    }
    Line {
        objectName: "lastLaunchFilters"
        Kirigami.FormData.label: "In-game filters:"
        text: form.editor.lastLaunchFilters
    }
}
