// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A game's portrait cover from Steam's cache, or a plain tile with the
// game's first letter when Steam has none.
Item {
    id: cover

    property url source
    property string name

    Rectangle {
        anchors.fill: parent
        visible: image.status !== Image.Ready
        color: Kirigami.Theme.alternateBackgroundColor
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)

        QQC2.Label {
            anchors.centerIn: parent
            text: cover.name.length > 0 ? cover.name.charAt(0).toUpperCase() : ""
            font.pixelSize: Math.round(parent.height * 0.35)
            opacity: 0.5
        }
    }

    Image {
        id: image
        anchors.fill: parent
        source: cover.source
        // Decode at display size, not the full 600x900.
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: status === Image.Ready
    }
}
