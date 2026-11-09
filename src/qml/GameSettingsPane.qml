// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Gfxtune.Backend
import org.kde.ki18n

// The settings page, for All Games or for one game.
QQC2.ScrollView {
    id: pane

    required property GameSettingsEditor editor
    // The scroll bar is drawn over the content; keep the settings clear of it.
    readonly property real barWidth: QQC2.ScrollBar.vertical.visible ? QQC2.ScrollBar.vertical.width : 0
    readonly property bool gamescopeUsable: editor.gamescope && editor.gamescopeAvailable
    // gamescope's upscalers only apply when the game is drawn smaller.
    readonly property bool upscaling: gamescopeUsable && editor.renderScale <= 100

    // A warning that wraps inside the form at a fixed width: sized by its own
    // text, it would chase the form's width around (Kirigami lays out its
    // message by that width).
    component FormWarning: Kirigami.InlineMessage {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 24
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 24
        type: Kirigami.MessageType.Warning
    }

    leftPadding: mirrored ? barWidth : 0
    rightPadding: mirrored ? 0 : barWidth
    contentWidth: availableWidth

    ColumnLayout {
        width: pane.availableWidth
        spacing: Kirigami.Units.largeSpacing

        PageHeader {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.gridUnit
            editor: pane.editor
        }

        LaunchStatus {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: !pane.editor.global
            editor: pane.editor
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
                Kirigami.FormData.label: KI18n.i18n("Image")
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Anisotropic filtering:")
                editor: pane.editor
                setting: "anisotropicFiltering"
                controlEnabled: pane.editor.anisotropicAvailable
                model: [
                    {text: KI18n.i18n("Game decides"), value: 0},
                    {text: KI18n.i18n("2×"), value: 2},
                    {text: KI18n.i18n("4×"), value: 4},
                    {text: KI18n.i18n("8×"), value: 8},
                    {text: KI18n.i18n("16×"), value: 16}
                ]
                value: pane.editor.anisotropicFiltering
                onPicked: value => pane.editor.anisotropicFiltering = value
            }
            Note {
                text: pane.editor.anisotropicNote
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Texture detail:")
                editor: pane.editor
                setting: "lodBias"
                controlName: "lodBias"
                controlEnabled: pane.editor.lodBiasAvailable
                // Hundredths of a mip level, added to the game's own bias.
                model: [
                    {text: KI18n.i18n("Game decides"), value: 0},
                    {text: KI18n.i18n("Sharper (−0.25)"), value: -25},
                    {text: KI18n.i18n("Sharper (−0.5)"), value: -50},
                    {text: KI18n.i18n("Sharper (−0.75)"), value: -75},
                    {text: KI18n.i18n("Sharper (−1)"), value: -100},
                    {text: KI18n.i18n("Sharper (−1.5)"), value: -150},
                    {text: KI18n.i18n("Sharpest (−2)"), value: -200},
                    {text: KI18n.i18n("Softer (+0.5)"), value: 50},
                    {text: KI18n.i18n("Softer (+1)"), value: 100}
                ]
                value: pane.editor.lodBias
                onPicked: value => pane.editor.lodBias = value
            }
            Note {
                text: pane.editor.lodBiasNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: KI18n.i18n("Frame pacing")
            }
            RowLayout {
                Kirigami.FormData.label: KI18n.i18n("Frame rate cap:")

                SettingSwitch {
                    enabled: pane.editor.frameCapAvailable
                    value: pane.editor.frameCapEnabled
                    onPicked: value => pane.editor.frameCapEnabled = value
                }
                QQC2.SpinBox {
                    id: capBox
                    wheelEnabled: false // scrolling the page must not edit it
                    enabled: pane.editor.frameCapAvailable && pane.editor.frameCapEnabled
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
                    value: pane.editor.baseValues.frameCapEnabled ? KI18n.i18n("%1 FPS", pane.editor.baseValues.frameCap) : KI18n.i18n("Off")
                }
            }
            Note {
                text: pane.editor.frameCapNote
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Vertical sync:")
                editor: pane.editor
                setting: "verticalSync"
                controlName: "verticalSync"
                controlEnabled: pane.editor.verticalSyncAvailable
                model: [
                    {text: KI18n.i18n("Game decides"), value: 0},
                    {text: KI18n.i18n("Off (may tear)"), value: 1},
                    {text: KI18n.i18n("On"), value: 2},
                    {text: KI18n.i18n("Fast (no tearing)"), value: 3},
                    {text: KI18n.i18n("Adaptive"), value: 4}
                ]
                value: pane.editor.verticalSync
                onPicked: value => pane.editor.verticalSync = value
            }
            Note {
                text: pane.editor.verticalSyncNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: KI18n.i18n("Upscaling")
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("FSR 4:")
                editor: pane.editor
                setting: "fsr4"
                controlName: "fsr4"
                controlEnabled: pane.editor.fsr4Available
                model: [
                    {text: pane.editor.fsr4ProtonChoice, value: 0},
                    {text: KI18n.i18n("On"), value: 1},
                    {text: KI18n.i18n("Off (keep FSR 3.1)"), value: 2}
                ]
                value: pane.editor.fsr4
                onPicked: value => pane.editor.fsr4 = value
            }
            Note {
                text: pane.editor.fsr4Note
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: KI18n.i18n("In-game filters")
            }
            FormWarning {
                objectName: "filtersWarning"
                visible: pane.editor.filtersLocked
                text: pane.editor.filtersWarning
                actions: [
                    Kirigami.Action {
                        text: KI18n.i18n("I Accept the Risk")
                        icon.name: "security-medium"
                        onTriggered: pane.editor.acceptFiltersRisk()
                    }
                ]
            }
            SwitchSetting {
                id: filters
                Kirigami.FormData.label: KI18n.i18n("Filters:")
                editor: pane.editor
                setting: "filtersEnabled"
                controlName: "filtersSwitch"
                controlEnabled: pane.editor.filtersAvailable && !pane.editor.filtersLocked
                text: value ? KI18n.i18n("On") : KI18n.i18n("Off")
                // Locked by anti-cheat, nothing is drawn whatever is saved.
                value: pane.editor.filtersEnabled && controlEnabled
                onPicked: value => pane.editor.filtersEnabled = value
            }
            Note {
                visible: filters.controlEnabled
                text: KI18n.i18n("Changes show in the game within a second while it runs. Switch off to compare with the original picture.")
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Anti-aliasing:")
                editor: pane.editor
                setting: "antiAliasing"
                controlName: "antiAliasing"
                controlEnabled: filters.controlEnabled
                model: [
                    {text: KI18n.i18n("Off"), value: 0},
                    {text: "SMAA", value: 1}
                ]
                value: pane.editor.antiAliasing
                onPicked: value => pane.editor.antiAliasing = value
            }
            Note {
                visible: filters.controlEnabled && pane.editor.antiAliasing !== 0
                text: KI18n.i18n("Smooths jagged edges in the finished picture. Costs a little performance and can soften small text.")
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Sharpening:")
                editor: pane.editor
                setting: "filterSharpening"
                controlName: "filterSharpening"
                controlEnabled: filters.controlEnabled
                from: 0
                to: 100
                describe: v => v === 0 ? KI18n.i18n("Off") : KI18n.i18nc("percent", "%1 %", v)
                value: pane.editor.filterSharpening
                onPicked: value => pane.editor.filterSharpening = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Brightness:")
                editor: pane.editor
                setting: "filterBrightness"
                controlEnabled: filters.controlEnabled
                from: -50
                to: 50
                value: pane.editor.filterBrightness
                onPicked: value => pane.editor.filterBrightness = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Contrast:")
                editor: pane.editor
                setting: "filterContrast"
                controlEnabled: filters.controlEnabled
                from: -50
                to: 50
                value: pane.editor.filterContrast
                onPicked: value => pane.editor.filterContrast = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Gamma:")
                editor: pane.editor
                setting: "filterGamma"
                controlEnabled: filters.controlEnabled
                from: 50
                to: 200
                describe: v => (v / 100).toFixed(2)
                value: pane.editor.filterGamma
                onPicked: value => pane.editor.filterGamma = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Saturation:")
                editor: pane.editor
                setting: "filterSaturation"
                controlEnabled: filters.controlEnabled
                from: -100
                to: 100
                value: pane.editor.filterSaturation
                onPicked: value => pane.editor.filterSaturation = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Vibrance:")
                editor: pane.editor
                setting: "filterVibrance"
                controlEnabled: filters.controlEnabled
                from: -100
                to: 100
                value: pane.editor.filterVibrance
                onPicked: value => pane.editor.filterVibrance = value
            }
            SliderSetting {
                Kirigami.FormData.label: KI18n.i18n("Colour temperature:")
                editor: pane.editor
                setting: "filterTemperature"
                controlEnabled: filters.controlEnabled
                from: -100
                to: 100
                describe: v => v === 0 ? KI18n.i18n("Neutral") : v > 0 ? KI18n.i18n("Warmer %1", v) : KI18n.i18n("Cooler %1", -v)
                value: pane.editor.filterTemperature
                onPicked: value => pane.editor.filterTemperature = value
            }
            QQC2.Button {
                enabled: filters.controlEnabled && !pane.editor.filterColoursNeutral
                text: KI18n.i18n("Reset Colours")
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
            SwitchSetting {
                Kirigami.FormData.label: KI18n.i18n("Run in gamescope:")
                editor: pane.editor
                setting: "gamescope"
                controlEnabled: pane.editor.gamescopeAvailable
                value: pane.editor.gamescope
                onPicked: value => pane.editor.gamescope = value
            }
            Note {
                text: pane.editor.gamescopeNote
            }
            FormWarning {
                visible: pane.editor.gamescope
                text: KI18n.i18n("gamescope can lower performance and add input latency. Turn it off if the game feels slower.")
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Render resolution:")
                editor: pane.editor
                setting: "renderScale"
                controlEnabled: pane.gamescopeUsable
                model: [
                    {text: KI18n.i18n("Supersampling (200%)"), value: 200},
                    {text: KI18n.i18n("Supersampling (150%)"), value: 150},
                    {text: KI18n.i18n("Supersampling (125%)"), value: 125},
                    {text: KI18n.i18n("Native (100%)"), value: 100},
                    {text: KI18n.i18n("Ultra quality (77%)"), value: 77},
                    {text: KI18n.i18n("Quality (67%)"), value: 67},
                    {text: KI18n.i18n("Balanced (59%)"), value: 59},
                    {text: KI18n.i18n("Performance (50%)"), value: 50},
                    {text: KI18n.i18n("Ultra performance (33%)"), value: 33}
                ]
                value: pane.editor.renderScale
                onPicked: value => pane.editor.renderScale = value
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Upscaler:")
                editor: pane.editor
                setting: "upscaleFilter"
                controlEnabled: pane.upscaling
                model: [
                    {text: "FSR 1", value: 0},
                    {text: "NIS", value: 1},
                    {text: "Snapdragon GSR", value: 2},
                    {text: KI18n.i18n("Linear"), value: 3},
                    {text: KI18n.i18n("Nearest neighbor"), value: 4},
                    {text: KI18n.i18n("Pixel art"), value: 5}
                ]
                value: pane.editor.upscaleFilter
                onPicked: value => pane.editor.upscaleFilter = value
            }
            RowLayout {
                Kirigami.FormData.label: KI18n.i18n("Upscaler sharpness:")

                QQC2.Slider {
                    id: sharpnessSlider
                    wheelEnabled: false // scrolling the page must not edit it
                    enabled: pane.upscaling && pane.editor.upscaleFilter === 0
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
                    enabled: sharpnessSlider.enabled
                    text: KI18n.i18n("%1 / 20", pane.editor.sharpness)
                    font.features: { "tnum": 1 }
                }
                BackToBase {
                    editor: pane.editor
                    setting: "sharpness"
                    value: KI18n.i18n("%1 / 20", pane.editor.baseValues.sharpness)
                }
            }
            ChoiceSetting {
                Kirigami.FormData.label: KI18n.i18n("Scaling:")
                editor: pane.editor
                setting: "scalingMode"
                controlEnabled: pane.gamescopeUsable
                model: [
                    {text: KI18n.i18n("Automatic"), value: 0},
                    {text: KI18n.i18n("Whole multiples (sharp pixels)"), value: 1},
                    {text: KI18n.i18n("Fit, keep shape"), value: 2},
                    {text: KI18n.i18n("Fill, crop edges"), value: 3},
                    {text: KI18n.i18n("Stretch"), value: 4}
                ]
                value: pane.editor.scalingMode
                onPicked: value => pane.editor.scalingMode = value
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: KI18n.i18n("Launch")
            }
            SwitchSetting {
                Kirigami.FormData.label: KI18n.i18n("GameMode:")
                editor: pane.editor
                setting: "gameMode"
                controlName: "gameMode"
                controlEnabled: pane.editor.gameModeAvailable
                value: pane.editor.gameMode
                onPicked: value => pane.editor.gameMode = value
            }
            Note {
                text: pane.editor.gameModeNote
            }
            SwitchSetting {
                Kirigami.FormData.label: KI18n.i18n("MangoHud:")
                editor: pane.editor
                setting: "mangoHud"
                controlName: "mangoHud"
                controlEnabled: pane.editor.mangoHudAvailable
                value: pane.editor.mangoHud
                onPicked: value => pane.editor.mangoHud = value
            }
            Note {
                text: pane.editor.mangoHudNote
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: KI18n.i18n("Performance")
            }
            SwitchSetting {
                Kirigami.FormData.label: KI18n.i18n("Measure performance:")
                editor: pane.editor
                setting: "measurePerformance"
                controlName: "measurePerformance"
                controlEnabled: pane.editor.measureAvailable && !pane.editor.filtersLocked
                // Locked by anti-cheat, nothing is measured whatever is saved.
                value: pane.editor.measurePerformance && controlEnabled
                onPicked: value => pane.editor.measurePerformance = value
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

        LastLaunch {
            Layout.fillWidth: true
            // Narrow pages put the labels above the settings, edge to edge.
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: !pane.editor.global
            editor: pane.editor
        }

        QQC2.Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Kirigami.Units.gridUnit
            objectName: "resetButton"
            text: pane.editor.global ? KI18n.i18n("Reset Global Settings to Defaults")
                : pane.editor.usesGlobal ? KI18n.i18n("Use Global Settings for Everything")
                : KI18n.i18n("Clear This Game's Changes")
            icon.name: "edit-undo"
            enabled: pane.editor.customized
            onClicked: pane.editor.resetToDefaults()
        }
    }
}
