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
    // The scroll bar is drawn over the content; keep the settings clear of it.
    readonly property real barWidth: QQC2.ScrollBar.vertical.visible ? QQC2.ScrollBar.vertical.width : 0

    readonly property var base: editor.baseValues

    // The words a list of {text, value} choices shows for a value.
    function textOf(choices, value) {
        const found = choices.find(choice => choice.value === value);
        return found ? found.text : String(value);
    }
    function onOff(on) {
        return on ? "On" : "Off";
    }

    leftPadding: mirrored ? barWidth : 0
    rightPadding: mirrored ? 0 : barWidth
    contentWidth: availableWidth

    ColumnLayout {
        width: pane.availableWidth
        spacing: Kirigami.Units.largeSpacing

        // The global page's header.
        ColumnLayout {
            objectName: "globalHeader"
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.gridUnit
            visible: pane.editor.global
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                text: "All Games"
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "These settings apply to every game. What you change on a game's own page wins over them for that game, and a game can stop using them there."
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.gridUnit
            visible: !pane.editor.global
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
                QQC2.CheckBox {
                    objectName: "usesGlobal"
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    text: "Use the global settings"
                    checked: pane.editor.usesGlobal
                    onToggled: {
                        pane.editor.usesGlobal = checked;
                        checked = Qt.binding(() => pane.editor.usesGlobal);
                    }
                }
                Note {
                    readonly property int changes: pane.editor.changedSettings.length
                    text: !pane.editor.usesGlobal
                        ? "This game ignores the settings for All Games: gfxtune leaves it alone apart from its own changes."
                        : changes === 0 ? "Every setting comes from All Games."
                        : changes === 1 ? "1 setting is changed for this game; the rest come from All Games."
                        : changes + " settings are changed for this game; the rest come from All Games."
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
            visible: !pane.editor.global
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
                    return "gfxtune has nothing to do in this game, so Steam starts it as usual. Change something below and gfxtune adds itself to the game's Steam launch options.";
                }
            }
            // Only when it shows: Kirigami counts a hidden action too, and then
            // moves it below and back as the text wraps.
            actions: launch === 2 && pane.editor.steamRunning ? [copyLineAction] : []

            Kirigami.Action {
                id: copyLineAction
                text: "Copy Line"
                icon.name: "edit-copy"
                onTriggered: pane.editor.copyLaunchLine()
            }
        }

        QQC2.TextField {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: launchMessage.visible && launchMessage.launch === 2 && pane.editor.steamRunning
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
            // Narrow pages put the labels above the settings, edge to edge.
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Image"
            }
            RowLayout {
                Kirigami.FormData.label: "Anisotropic filtering:"
                ValueComboBox {
                    id: anisotropicBox
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
                BackToBase {
                    editor: pane.editor
                    setting: "anisotropicFiltering"
                    value: pane.textOf(anisotropicBox.model, pane.base.anisotropicFiltering)
                }
            }
            Note {
                text: pane.editor.anisotropicNote
            }
            RowLayout {
                Kirigami.FormData.label: "Texture detail:"
                ValueComboBox {
                    id: lodBiasBox
                    objectName: "lodBias"
                    enabled: pane.editor.lodBiasAvailable
                    // Hundredths of a mip level, added to the game's own bias.
                    model: [
                        {text: "Game decides", value: 0},
                        {text: "Sharper (−0.25)", value: -25},
                        {text: "Sharper (−0.5)", value: -50},
                        {text: "Sharper (−0.75)", value: -75},
                        {text: "Sharper (−1)", value: -100},
                        {text: "Sharper (−1.5)", value: -150},
                        {text: "Sharpest (−2)", value: -200},
                        {text: "Softer (+0.5)", value: 50},
                        {text: "Softer (+1)", value: 100}
                    ]
                    value: pane.editor.lodBias
                    onPicked: value => pane.editor.lodBias = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "lodBias"
                    value: pane.textOf(lodBiasBox.model, pane.base.lodBias)
                }
            }
            Note {
                text: pane.editor.lodBiasNote
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
                BackToBase {
                    editor: pane.editor
                    setting: "frameCap"
                    value: pane.base.frameCapEnabled ? pane.base.frameCap + " FPS" : "Off"
                }
            }
            Note {
                text: pane.editor.frameCapNote
            }
            RowLayout {
                Kirigami.FormData.label: "Vertical sync:"
                ValueComboBox {
                    id: verticalSyncBox
                    objectName: "verticalSync"
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
                BackToBase {
                    editor: pane.editor
                    setting: "verticalSync"
                    value: pane.textOf(verticalSyncBox.model, pane.base.verticalSync)
                }
            }
            Note {
                text: pane.editor.verticalSyncNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Upscaling"
            }
            RowLayout {
                Kirigami.FormData.label: "FSR 4:"
                ValueComboBox {
                    id: fsr4Box
                    objectName: "fsr4"
                    enabled: pane.editor.fsr4Available
                    model: [
                        {text: pane.editor.fsr4ProtonChoice, value: 0},
                        {text: "On", value: 1},
                        {text: "Off (keep FSR 3.1)", value: 2}
                    ]
                    value: pane.editor.fsr4
                    onPicked: value => pane.editor.fsr4 = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "fsr4"
                    value: pane.textOf(fsr4Box.model, pane.base.fsr4)
                }
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
                // A set width: sized by its own text, it would chase the form's
                // width around (Kirigami lays out its message by that width).
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
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
            RowLayout {
                Kirigami.FormData.label: "Filters:"
                SettingSwitch {
                    id: filtersSwitch
                    objectName: "filtersSwitch"
                    enabled: pane.editor.filtersAvailable && !pane.editor.filtersLocked
                    text: checked ? "On" : "Off"
                    // Locked by anti-cheat, nothing is drawn whatever is saved.
                    value: pane.editor.filtersEnabled && enabled
                    onPicked: value => pane.editor.filtersEnabled = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filtersEnabled"
                    value: pane.onOff(pane.base.filtersEnabled)
                }
            }
            Note {
                visible: filtersSwitch.enabled
                text: "Changes show in the game within a second while it runs. Switch off to compare with the original picture."
            }
            RowLayout {
                Kirigami.FormData.label: "Anti-aliasing:"
                ValueComboBox {
                    id: antiAliasingBox
                    objectName: "antiAliasing"
                    enabled: filtersSwitch.enabled
                    model: [
                        {text: "Off", value: 0},
                        {text: "SMAA", value: 1}
                    ]
                    value: pane.editor.antiAliasing
                    onPicked: value => pane.editor.antiAliasing = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "antiAliasing"
                    value: pane.textOf(antiAliasingBox.model, pane.base.antiAliasing)
                }
            }
            Note {
                visible: filtersSwitch.enabled && pane.editor.antiAliasing !== 0
                text: "Smooths jagged edges in the finished picture. Costs a little performance and can soften small text."
            }
            RowLayout {
                Kirigami.FormData.label: "Sharpening:"
                FilterSlider {
                    id: filterSharpeningSlider
                    objectName: "filterSharpening"
                    enabled: filtersSwitch.enabled
                    from: 0
                    to: 100
                    value: pane.editor.filterSharpening
                    describe: v => v === 0 ? "Off" : v + " %"
                    onPicked: value => pane.editor.filterSharpening = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterSharpening"
                    value: filterSharpeningSlider.describe(pane.base.filterSharpening)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Brightness:"
                FilterSlider {
                    id: filterBrightnessSlider
                    enabled: filtersSwitch.enabled
                    from: -50
                    to: 50
                    value: pane.editor.filterBrightness
                    onPicked: value => pane.editor.filterBrightness = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterBrightness"
                    value: filterBrightnessSlider.describe(pane.base.filterBrightness)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Contrast:"
                FilterSlider {
                    id: filterContrastSlider
                    enabled: filtersSwitch.enabled
                    from: -50
                    to: 50
                    value: pane.editor.filterContrast
                    onPicked: value => pane.editor.filterContrast = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterContrast"
                    value: filterContrastSlider.describe(pane.base.filterContrast)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Gamma:"
                FilterSlider {
                    id: filterGammaSlider
                    enabled: filtersSwitch.enabled
                    from: 50
                    to: 200
                    value: pane.editor.filterGamma
                    describe: v => (v / 100).toFixed(2)
                    onPicked: value => pane.editor.filterGamma = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterGamma"
                    value: filterGammaSlider.describe(pane.base.filterGamma)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Saturation:"
                FilterSlider {
                    id: filterSaturationSlider
                    enabled: filtersSwitch.enabled
                    from: -100
                    to: 100
                    value: pane.editor.filterSaturation
                    onPicked: value => pane.editor.filterSaturation = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterSaturation"
                    value: filterSaturationSlider.describe(pane.base.filterSaturation)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Vibrance:"
                FilterSlider {
                    id: filterVibranceSlider
                    enabled: filtersSwitch.enabled
                    from: -100
                    to: 100
                    value: pane.editor.filterVibrance
                    onPicked: value => pane.editor.filterVibrance = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterVibrance"
                    value: filterVibranceSlider.describe(pane.base.filterVibrance)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Colour temperature:"
                FilterSlider {
                    id: filterTemperatureSlider
                    enabled: filtersSwitch.enabled
                    from: -100
                    to: 100
                    value: pane.editor.filterTemperature
                    describe: v => v === 0 ? "Neutral" : (v > 0 ? "Warmer " : "Cooler ") + Math.abs(v)
                    onPicked: value => pane.editor.filterTemperature = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "filterTemperature"
                    value: filterTemperatureSlider.describe(pane.base.filterTemperature)
                }
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
            RowLayout {
                Kirigami.FormData.label: "Run in gamescope:"
                SettingSwitch {
                    enabled: pane.editor.gamescopeAvailable
                    value: pane.editor.gamescope
                    onPicked: value => pane.editor.gamescope = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "gamescope"
                    value: pane.onOff(pane.base.gamescope)
                }
            }
            Note {
                text: pane.editor.gamescopeNote
            }
            Kirigami.InlineMessage {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24 // see the filters warning
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.gamescope
                type: Kirigami.MessageType.Warning
                text: "gamescope can lower performance and add input latency. Turn it off if the game feels slower."
            }
            RowLayout {
                Kirigami.FormData.label: "Render resolution:"
                ValueComboBox {
                    id: renderScaleBox
                    enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable
                    model: [
                        {text: "Supersampling (200%)", value: 200},
                        {text: "Supersampling (150%)", value: 150},
                        {text: "Supersampling (125%)", value: 125},
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
                BackToBase {
                    editor: pane.editor
                    setting: "renderScale"
                    value: pane.textOf(renderScaleBox.model, pane.base.renderScale)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Upscaler:"
                ValueComboBox {
                    id: upscalerBox
                    enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable && pane.editor.renderScale <= 100
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
                BackToBase {
                    editor: pane.editor
                    setting: "upscaleFilter"
                    value: pane.textOf(upscalerBox.model, pane.base.upscaleFilter)
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Upscaler sharpness:"
                enabled: pane.editor.gamescope && pane.editor.gamescopeAvailable && pane.editor.renderScale <= 100
                         && pane.editor.upscaleFilter === 0

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
                BackToBase {
                    editor: pane.editor
                    setting: "sharpness"
                    value: pane.base.sharpness + " / 20"
                }
            }
            RowLayout {
                Kirigami.FormData.label: "Scaling:"
                ValueComboBox {
                    id: scalingBox
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
                BackToBase {
                    editor: pane.editor
                    setting: "scalingMode"
                    value: pane.textOf(scalingBox.model, pane.base.scalingMode)
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Launch"
            }
            RowLayout {
                Kirigami.FormData.label: "GameMode:"
                SettingSwitch {
                    objectName: "gameMode"
                    enabled: pane.editor.gameModeAvailable
                    value: pane.editor.gameMode
                    onPicked: value => pane.editor.gameMode = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "gameMode"
                    value: pane.onOff(pane.base.gameMode)
                }
            }
            Note {
                text: pane.editor.gameModeNote
            }
            RowLayout {
                Kirigami.FormData.label: "MangoHud:"
                SettingSwitch {
                    objectName: "mangoHud"
                    enabled: pane.editor.mangoHudAvailable
                    value: pane.editor.mangoHud
                    onPicked: value => pane.editor.mangoHud = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "mangoHud"
                    value: pane.onOff(pane.base.mangoHud)
                }
            }
            Note {
                text: pane.editor.mangoHudNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Performance"
            }
            RowLayout {
                Kirigami.FormData.label: "Measure performance:"
                SettingSwitch {
                    objectName: "measurePerformance"
                    enabled: pane.editor.measureAvailable && !pane.editor.filtersLocked
                    // Locked by anti-cheat, nothing is measured whatever is saved.
                    value: pane.editor.measurePerformance && enabled
                    onPicked: value => pane.editor.measurePerformance = value
                }
                BackToBase {
                    editor: pane.editor
                    setting: "measurePerformance"
                    value: pane.onOff(pane.base.measurePerformance)
                }
            }
            Note {
                text: pane.editor.measureNote
            }
        }

        PerformanceHistory {
            visible: !pane.editor.global
            Layout.fillWidth: true
            // A column is otherwise no wider than its widest child; filling
            // the page centres the table under the settings.
            Layout.maximumWidth: Number.POSITIVE_INFINITY
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            editor: pane.editor
            room: pane.availableWidth - Layout.leftMargin - Layout.rightMargin
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            // Narrow pages put the labels above the settings, edge to edge.
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: !pane.editor.global

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
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.hasLastLaunch
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchApplied.length > 0 ? pane.editor.lastLaunchApplied : "Nothing"
            }
            QQC2.Label {
                Kirigami.FormData.label: "Left to Steam's options:"
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchKept.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchKept
            }
            QQC2.Label {
                Kirigami.FormData.label: "Not applied:"
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchNotApplied.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchNotApplied
            }
            QQC2.Label {
                objectName: "lastLaunchFilters"
                Kirigami.FormData.label: "In-game filters:"
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                visible: pane.editor.lastLaunchFilters.length > 0
                wrapMode: Text.Wrap
                text: pane.editor.lastLaunchFilters
            }
        }

        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Kirigami.Units.gridUnit
            objectName: "resetButton"
            text: pane.editor.global ? "Reset Global Settings to Defaults"
                : pane.editor.usesGlobal ? "Use Global Settings for Everything"
                : "Clear This Game's Changes"
            icon.name: "edit-undo"
            enabled: pane.editor.customized
            onClicked: pane.editor.resetToDefaults()
        }
    }
}
