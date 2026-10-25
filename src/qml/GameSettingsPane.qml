// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend

QQC2.ScrollView {
    id: pane

    required property GameSettingsEditor editor

    contentWidth: availableWidth

    ColumnLayout {
        width: pane.availableWidth
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.gridUnit

            Cover {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 7.5
                source: pane.editor.cover
                name: pane.editor.gameName
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 1
                    text: pane.editor.gameName
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    text: pane.editor.runsAs
                }
                QQC2.Label {
                    text: pane.editor.graphics
                    opacity: 0.7
                }
            }
        }

        // Where the game stands with gfxtune-run in its Steam launch options.
        // The numbers follow GameLibrary::LaunchState.
        Kirigami.InlineMessage {
            id: launchMessage

            readonly property int launch: pane.editor.launchState

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: true
            type: launch === 1 ? Kirigami.MessageType.Positive
                : launch === 4 ? Kirigami.MessageType.Error
                : Kirigami.MessageType.Information
            text: {
                switch (launch) {
                case 1:
                    return "gfxtune is in this game's Steam launch options. Changes apply the next time the game starts.";
                case 2:
                    return pane.editor.steamRunning
                        ? "Steam is open, so gfxtune adds itself to this game's launch options once Steam closes. To use these settings sooner, paste this line into the game's launch options in Steam:"
                        : "Adding gfxtune to this game's Steam launch options…";
                case 3:
                    return pane.editor.steamRunning
                        ? "gfxtune takes itself out of this game's launch options once Steam closes. Until then the game starts with its own settings."
                        : "Taking gfxtune out of this game's Steam launch options…";
                case 4:
                    return "Steam's launch options could not be updated: " + pane.editor.launchProblem;
                default:
                    return "The game uses its own settings. Change something below and gfxtune adds itself to the game's Steam launch options.";
                }
            }
            actions: [
                Kirigami.Action {
                    visible: launchMessage.launch === 2 && pane.editor.steamRunning
                    text: "Copy Line"
                    icon.name: "edit-copy"
                    onTriggered: pane.editor.copyLaunchLine()
                }
            ]
        }

        QQC2.TextField {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: launchMessage.launch === 2 && pane.editor.steamRunning
            readOnly: true
            text: pane.editor.launchLine
            font.family: "monospace"
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: pane.editor.problem.length > 0
            type: Kirigami.MessageType.Error
            text: pane.editor.problem
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Image"
            }
            ValueComboBox {
                Kirigami.FormData.label: "Anisotropic filtering:"
                enabled: pane.editor.anisotropicAvailable
                model: [
                    {text: "Game decides", value: 0},
                    {text: "2×", value: 2},
                    {text: "4×", value: 4},
                    {text: "8×", value: 8},
                    {text: "16×", value: 16}
                ]
                value: pane.editor.anisotropicFiltering
                onPicked: value => pane.editor.anisotropicFiltering = value
            }
            Note {
                text: pane.editor.anisotropicNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Frame pacing"
            }
            RowLayout {
                Kirigami.FormData.label: "Frame rate cap:"
                enabled: pane.editor.frameCapAvailable

                SettingSwitch {
                    value: pane.editor.frameCapEnabled
                    onPicked: value => pane.editor.frameCapEnabled = value
                }
                QQC2.SpinBox {
                    id: capBox
                    wheelEnabled: false // scrolling the page must not edit it
                    enabled: pane.editor.frameCapEnabled
                    from: 10
                    to: 1000
                    editable: true
                    value: pane.editor.frameCap
                    onValueModified: {
                        pane.editor.frameCap = value;
                        value = Qt.binding(() => pane.editor.frameCap);
                    }
                }
                QQC2.Label {
                    text: "FPS"
                    enabled: capBox.enabled
                }
            }
            Note {
                text: pane.editor.frameCapNote
            }
            ValueComboBox {
                objectName: "verticalSync"
                Kirigami.FormData.label: "Vertical sync:"
                enabled: pane.editor.verticalSyncAvailable
                model: [
                    {text: "Game decides", value: 0},
                    {text: "Off (may tear)", value: 1},
                    {text: "On", value: 2},
                    {text: "Fast (no tearing)", value: 3},
                    {text: "Adaptive", value: 4}
                ]
                value: pane.editor.verticalSync
                onPicked: value => pane.editor.verticalSync = value
            }
            Note {
                text: pane.editor.verticalSyncNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Upscaling"
            }
            ValueComboBox {
                objectName: "fsr4"
                Kirigami.FormData.label: "FSR 4:"
                enabled: pane.editor.fsr4Available
                model: [
                    {text: pane.editor.fsr4ProtonChoice, value: 0},
                    {text: "On", value: 1},
                    {text: "Off (keep FSR 3.1)", value: 2}
                ]
                value: pane.editor.fsr4
                onPicked: value => pane.editor.fsr4 = value
            }
            Note {
                text: pane.editor.fsr4Note
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "In-game filters"
            }
            Kirigami.InlineMessage {
                objectName: "filtersWarning"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.filtersLocked
                type: Kirigami.MessageType.Warning
                text: pane.editor.filtersWarning
                actions: [
                    Kirigami.Action {
                        text: "I Accept the Risk"
                        icon.name: "security-medium"
                        onTriggered: pane.editor.acceptFiltersRisk()
                    }
                ]
            }
            SettingSwitch {
                id: filtersSwitch
                objectName: "filtersSwitch"
                Kirigami.FormData.label: "Filters:"
                enabled: pane.editor.filtersAvailable && !pane.editor.filtersLocked
                text: checked ? "On" : "Off"
                value: pane.editor.filtersEnabled
                onPicked: value => pane.editor.filtersEnabled = value
            }
            Note {
                visible: filtersSwitch.enabled
                text: "Changes show in the game within a second while it runs. Switch off to compare with the original picture."
            }
            FilterSlider {
                objectName: "filterSharpening"
                Kirigami.FormData.label: "Sharpening:"
                enabled: filtersSwitch.enabled
                from: 0
                to: 100
                value: pane.editor.filterSharpening
                describe: v => v === 0 ? "Off" : v + " %"
                onPicked: value => pane.editor.filterSharpening = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Brightness:"
                enabled: filtersSwitch.enabled
                from: -50
                to: 50
                value: pane.editor.filterBrightness
                onPicked: value => pane.editor.filterBrightness = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Contrast:"
                enabled: filtersSwitch.enabled
                from: -50
                to: 50
                value: pane.editor.filterContrast
                onPicked: value => pane.editor.filterContrast = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Gamma:"
                enabled: filtersSwitch.enabled
                from: 50
                to: 200
                value: pane.editor.filterGamma
                describe: v => (v / 100).toFixed(2)
                onPicked: value => pane.editor.filterGamma = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Saturation:"
                enabled: filtersSwitch.enabled
                from: -100
                to: 100
                value: pane.editor.filterSaturation
                onPicked: value => pane.editor.filterSaturation = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Vibrance:"
                enabled: filtersSwitch.enabled
                from: -100
                to: 100
                value: pane.editor.filterVibrance
                onPicked: value => pane.editor.filterVibrance = value
            }
            FilterSlider {
                Kirigami.FormData.label: "Colour temperature:"
                enabled: filtersSwitch.enabled
                from: -100
                to: 100
                value: pane.editor.filterTemperature
                describe: v => v === 0 ? "Neutral" : (v > 0 ? "Warmer " : "Cooler ") + Math.abs(v)
                onPicked: value => pane.editor.filterTemperature = value
            }
            QQC2.Button {
                enabled: filtersSwitch.enabled && !pane.editor.filterColoursNeutral
                text: "Reset Colours"
                icon.name: "edit-undo"
                flat: true
                onClicked: pane.editor.resetFilterColours()
            }
            Note {
                text: pane.editor.filtersNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "gamescope"
            }
            SettingSwitch {
                Kirigami.FormData.label: "Run in gamescope:"
                enabled: pane.editor.gamescopeAvailable
                value: pane.editor.gamescope
                onPicked: value => pane.editor.gamescope = value
            }
            Note {
                text: pane.editor.gamescopeNote
            }
            Kirigami.InlineMessage {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.gamescope
                type: Kirigami.MessageType.Warning
                text: "gamescope can lower performance and add input latency. Turn it off if the game feels slower."
            }
            ValueComboBox {
                Kirigami.FormData.label: "Render resolution:"
                enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable
                model: [
                    {text: "Native (100%)", value: 100},
                    {text: "Ultra quality (77%)", value: 77},
                    {text: "Quality (67%)", value: 67},
                    {text: "Balanced (59%)", value: 59},
                    {text: "Performance (50%)", value: 50},
                    {text: "Ultra performance (33%)", value: 33}
                ]
                value: pane.editor.renderScale
                onPicked: value => pane.editor.renderScale = value
            }
            ValueComboBox {
                Kirigami.FormData.label: "Upscaler:"
                enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable
                model: [
                    {text: "FSR 1", value: 0},
                    {text: "NIS", value: 1},
                    {text: "Snapdragon GSR", value: 2},
                    {text: "Linear", value: 3},
                    {text: "Nearest neighbor", value: 4},
                    {text: "Pixel art", value: 5}
                ]
                value: pane.editor.upscaleFilter
                onPicked: value => pane.editor.upscaleFilter = value
            }
            RowLayout {
                Kirigami.FormData.label: "Upscaler sharpness:"
                enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable && pane.editor.upscaleFilter === 0

                QQC2.Slider {
                    wheelEnabled: false // scrolling the page must not edit it
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    from: 0
                    to: 20
                    stepSize: 1
                    snapMode: QQC2.Slider.SnapAlways
                    value: pane.editor.sharpness
                    onMoved: {
                        pane.editor.sharpness = value;
                        value = Qt.binding(() => pane.editor.sharpness);
                    }
                }
                QQC2.Label {
                    text: pane.editor.sharpness + " / 20"
                    font.features: { "tnum": 1 }
                }
            }
            ValueComboBox {
                Kirigami.FormData.label: "Scaling:"
                enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable
                model: [
                    {text: "Automatic", value: 0},
                    {text: "Whole multiples (sharp pixels)", value: 1},
                    {text: "Fit, keep shape", value: 2},
                    {text: "Fill, crop edges", value: 3},
                    {text: "Stretch", value: 4}
                ]
                value: pane.editor.scalingMode
                onPicked: value => pane.editor.scalingMode = value
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Last launch"
            }
            QQC2.Label {
                visible: !pane.editor.hasLastLaunch
                text: "Not started through gfxtune yet."
                opacity: 0.7
            }
            QQC2.Label {
                Kirigami.FormData.label: "Started:"
                visible: pane.editor.hasLastLaunch
                text: pane.editor.lastLaunchTime
            }
            QQC2.Label {
                Kirigami.FormData.label: "Applied:"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.hasLastLaunch
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchApplied.length > 0 ? pane.editor.lastLaunchApplied : "Nothing"
            }
            QQC2.Label {
                Kirigami.FormData.label: "Left to Steam's options:"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchKept.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchKept
            }
            QQC2.Label {
                Kirigami.FormData.label: "Not applied:"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchNotApplied.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchNotApplied
            }
            QQC2.Label {
                objectName: "lastLaunchFilters"
                Kirigami.FormData.label: "In-game filters:"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchFilters.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchFilters
            }
        }

        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Kirigami.Units.gridUnit
            text: "Reset This Game to Defaults"
            icon.name: "edit-undo"
            enabled: pane.editor.customized
            onClicked: pane.editor.resetToDefaults()
        }
    }
}
