// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.quickcharts as Charts
import Gfxtune.Backend

// The live readings on the right of the window: the game being measured,
// the card and the processor.
QQC2.ScrollView {
    id: panel

    required property GpuMonitor monitor
    required property GameLibrary library

    // The same colour as a Kirigami.Separator.
    readonly property color gridColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
    // The accent colour. Not highlightColor: KDE dims that while the window
    // is not focused, which is the usual case while a game runs.
    readonly property color mainColor: Kirigami.Theme.focusColor
    readonly property color hotspotColor: Kirigami.Theme.neutralTextColor
    readonly property bool cardShown: monitor.status === GpuMonitor.AmdCard && !monitor.asleep

    // The scroll bar is drawn over the content; keep the readings clear of it.
    readonly property real barWidth: QQC2.ScrollBar.vertical.visible ? QQC2.ScrollBar.vertical.width + Kirigami.Units.smallSpacing : 0
    leftPadding: mirrored ? barWidth : 0
    rightPadding: mirrored ? 0 : barWidth
    contentWidth: availableWidth
    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

    component Key: QQC2.Label {
        Layout.fillWidth: true
        opacity: 0.7
    }
    component Value: QQC2.Label {
        Layout.alignment: Qt.AlignRight
        horizontalAlignment: Text.AlignRight
        font.features: { "tnum": 1 }
    }
    component Readings: GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing
    }

    // The last minute of a reading, drawn like KDE's System Monitor: grid
    // lines at the bottom, middle and top of the range with their values on
    // the left, the newest reading at the right edge. An optional second line
    // is drawn without a fill, since two fills on top of each other mix.
    component HistoryChart: Item {
        id: chart

        required property string unit
        required property real low
        required property real high
        // Lists of numbers, oldest first. Plain var: the chart takes a
        // variant list, which a typed list<real> does not convert to.
        required property var values
        required property color lineColor
        property var secondValues: []
        property color secondColor

        readonly property real labelWidth: labelMetrics.width + Kirigami.Units.smallSpacing
        readonly property real plotWidth: width - labelWidth

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
        // Room for the halves of the top and bottom values outside the lines.
        Layout.topMargin: Math.ceil(labelMetrics.height / 2)
        Layout.bottomMargin: Math.ceil(labelMetrics.height / 2)

        TextMetrics {
            id: labelMetrics
            font: Kirigami.Theme.smallFont
            text: Math.round(chart.high) + " " + chart.unit
        }

        Repeater {
            model: 3
            delegate: Item {
                id: gridLine

                required property int index

                y: Math.round((chart.height - 1) * (1 - index / 2))
                width: chart.width

                Rectangle {
                    x: chart.labelWidth
                    width: chart.plotWidth
                    height: 1
                    color: panel.gridColor
                }
                QQC2.Label {
                    x: chart.labelWidth - width - Kirigami.Units.smallSpacing
                    y: -height / 2
                    font: Kirigami.Theme.smallFont
                    opacity: 0.75
                    text: Math.round(chart.low + (chart.high - chart.low) * gridLine.index / 2) + " " + chart.unit
                }
            }
        }

        // Spans only the readings there are, so the lines grow in from the
        // right after a start instead of dropping to zero where none exist.
        Item {
            id: plot

            readonly property int count: chart.values.length

            x: chart.width - width
            width: count > 1 ? chart.plotWidth * (count - 1) / (panel.monitor.historyLength - 1) : 0
            height: chart.height
            visible: count > 1

            Charts.LineChart {
                anchors.fill: parent
                interpolate: true
                lineWidth: 2
                fillOpacity: 0.2
                yRange {
                    from: chart.low
                    to: chart.high
                    automatic: false
                }
                valueSources: Charts.ArraySource { array: chart.values }
                colorSource: Charts.SingleValueSource { value: chart.lineColor }
            }
            Charts.LineChart {
                anchors.fill: parent
                visible: chart.secondValues.length === plot.count
                interpolate: true
                lineWidth: 2
                fillOpacity: 0
                yRange {
                    from: chart.low
                    to: chart.high
                    automatic: false
                }
                valueSources: Charts.ArraySource { array: chart.secondValues }
                colorSource: Charts.SingleValueSource { value: chart.secondColor }
            }
        }
    }

    component LegendEntry: RowLayout {
        id: entry

        required property color swatch
        required property string name
        required property string value

        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Math.round(Kirigami.Units.gridUnit * 0.6)
            implicitHeight: 3
            radius: 1
            color: entry.swatch
        }
        QQC2.Label {
            text: entry.name
            font: Kirigami.Theme.smallFont
            opacity: 0.75
        }
        QQC2.Label {
            text: entry.value
            font: Kirigami.Theme.smallFont
        }
    }

    component Caption: QQC2.Label {
        font: Kirigami.Theme.smallFont
        opacity: 0.75
    }

    ColumnLayout {
        width: panel.availableWidth
        spacing: Kirigami.Units.smallSpacing

        // The game the layer measures, while one runs.
        ColumnLayout {
            objectName: "gameBlock"
            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.gridUnit
            visible: panel.monitor.gameAppId !== 0
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 3
                text: panel.library.gameName(panel.monitor.gameAppId)
                elide: Text.ElideRight
            }
            Readings {
                Key { text: "Frame rate" }
                Value { text: panel.monitor.gameFps }
                Key { text: "Frame time" }
                Value { text: panel.monitor.gameFrameTime }
                Key { text: "Video memory" }
                Value { text: panel.monitor.gameVideoMemory }
            }
            Caption {
                Layout.topMargin: Kirigami.Units.largeSpacing
                text: "Frame rate, last minute"
            }
            HistoryChart {
                unit: "FPS"
                low: 0
                high: panel.monitor.fpsHigh
                values: panel.monitor.fpsHistory
                lineColor: panel.mainColor
            }
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 3
            text: "GPU"
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: panel.monitor.cardNames.length === 1
            text: panel.monitor.cardNames.length > 0 ? panel.monitor.cardNames[0] : ""
            elide: Text.ElideRight
            opacity: 0.7
        }

        QQC2.ComboBox {
            Layout.fillWidth: true
            visible: panel.monitor.cardNames.length > 1
            model: panel.monitor.cardNames
            currentIndex: panel.monitor.currentCard
            wheelEnabled: false
            onActivated: index => panel.monitor.currentCard = index
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: panel.monitor.status !== GpuMonitor.AmdCard
            icon.name: "video-card-inactive"
            text: panel.monitor.status === GpuMonitor.OtherCards ? "Only AMD cards are supported" : "No graphics card found"
            explanation: panel.monitor.status === GpuMonitor.OtherCards
                ? "gfxtune reads the sensors of AMD Radeon cards through the amdgpu driver."
                : ""
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: panel.monitor.status === GpuMonitor.AmdCard && panel.monitor.asleep
            text: "The card is asleep. gfxtune does not wake it just to read it."
            wrapMode: Text.Wrap
            opacity: 0.7
        }

        Readings {
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: panel.cardShown

            Key { text: "Temperature" }
            Value { text: panel.monitor.temperature }
            Key { text: "Hotspot" }
            Value { text: panel.monitor.hotspot }
            Key { text: "Memory temperature" }
            Value { text: panel.monitor.memoryTemperature }
            Key { text: "Power" }
            Value { text: panel.monitor.power }
            Key { text: "GPU load" }
            Value { text: panel.monitor.load }
            Key { text: "Memory load" }
            Value { text: panel.monitor.memoryLoad }
            Key { text: "Video memory" }
            Value { text: panel.monitor.videoMemory }
            Key { text: "Core clock" }
            Value { text: panel.monitor.coreClock }
            Key { text: "Memory clock" }
            Value { text: panel.monitor.memoryClock }
            Key { text: "Fan" }
            Value { text: panel.monitor.fan }
            Key { text: "Voltage" }
            Value { text: panel.monitor.voltage }
            Key {
                Layout.alignment: Qt.AlignTop
                text: "Held back by"
            }
            Value {
                objectName: "heldBackBy"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                wrapMode: Text.Wrap
                text: panel.monitor.heldBackBy
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: panel.cardShown
            spacing: Kirigami.Units.smallSpacing

            Caption {
                text: "Temperature, last minute"
            }
            HistoryChart {
                unit: "°C"
                low: panel.monitor.temperatureLow
                high: panel.monitor.temperatureHigh
                values: panel.monitor.edgeHistory
                lineColor: panel.mainColor
                secondValues: panel.monitor.hotspotHistory
                secondColor: panel.hotspotColor
            }
            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                LegendEntry {
                    swatch: panel.mainColor
                    name: "Temperature"
                    value: panel.monitor.temperature
                }
                LegendEntry {
                    swatch: panel.hotspotColor
                    name: "Hotspot"
                    value: panel.monitor.hotspot
                }
            }

            Caption {
                Layout.topMargin: Kirigami.Units.largeSpacing
                text: "Power, last minute"
            }
            HistoryChart {
                unit: "W"
                low: 0
                high: panel.monitor.powerHigh
                values: panel.monitor.powerHistory
                lineColor: panel.mainColor
            }
            LegendEntry {
                swatch: panel.mainColor
                name: "Power"
                value: panel.monitor.power
            }
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit
            level: 3
            text: "CPU"
        }
        Readings {
            objectName: "cpuReadings"

            Key { text: "Load" }
            Value { text: panel.monitor.cpuLoad }
            Key { text: "Temperature" }
            Value { text: panel.monitor.cpuTemperature }
            Key { text: "Clock" }
            Value { text: panel.monitor.cpuClock }
        }
    }
}
