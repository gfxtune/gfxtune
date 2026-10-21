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

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: true
            type: Kirigami.MessageType.Information
            text: "Settings are saved for this game. Applying them when the game starts comes in the next update of gfxtune."
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
            SettingSwitch {
                Kirigami.FormData.label: "FSR 4 upgrade:"
                enabled: pane.editor.fsr4Available
                value: pane.editor.fsr4Upgrade
                onPicked: value => pane.editor.fsr4Upgrade = value
            }
            Note {
                text: pane.editor.fsr4Note.length > 0 ? pane.editor.fsr4Note
                                                     : "Replaces the game's FSR 3.1 with FSR 4. Costs some performance on RDNA 3 cards."
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
