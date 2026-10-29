// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A short explanation under a setting: why it is unavailable, or what
// limits it. Hidden when there is nothing to say.
QQC2.Label {
    // Narrower on a narrow page, where it wraps.
    Layout.fillWidth: true
    Layout.maximumWidth: Kirigami.Units.gridUnit * 24
    visible: text.length > 0
    wrapMode: Text.Wrap
    font: Kirigami.Theme.smallFont
    opacity: 0.7
}
