// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtGraphs
import org.kde.kirigami as Kirigami
import Gfxtune.Backend

// The card's live readings, on the right of the window.
ColumnLayout {
    id: panel

    required property GpuMonitor monitor

    spacing: Kirigami.Units.smallSpacing

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

    GridLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        visible: panel.monitor.status === GpuMonitor.AmdCard && !panel.monitor.asleep
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        component Key: QQC2.Label {
            Layout.fillWidth: true
            opacity: 0.7
        }
        component Value: QQC2.Label {
            Layout.alignment: Qt.AlignRight
            font.features: { "tnum": 1 }
        }

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
    }

    component Chart: GraphsView {
        id: chart

        property real low
        property real high

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 7
        marginLeft: 0
        marginRight: 0
        marginTop: Kirigami.Units.smallSpacing
        marginBottom: Kirigami.Units.smallSpacing
        theme: GraphsTheme {
            backgroundVisible: false
            plotAreaBackgroundVisible: false
            labelTextColor: Kirigami.Theme.disabledTextColor
            grid.mainColor: Qt.alpha(Kirigami.Theme.textColor, 0.15)
            grid.subColor: "transparent"
            axisX.mainColor: "transparent"
            axisY.mainColor: "transparent"
        }
        axisX: ValueAxis {
            min: 0
            max: panel.monitor.historyLength - 1
            visible: false
            gridVisible: false
            subGridVisible: false
        }
        axisY: ValueAxis {
            min: chart.low
            max: chart.high
            tickInterval: (chart.high - chart.low) / 2
            tickAnchor: chart.low
            subGridVisible: false
            lineVisible: false
            labelDecimals: 0
        }
    }

    // Puts a history list into a line series, newest on the right.
    function fill(series, values) {
        series.clear();
        const offset = panel.monitor.historyLength - values.length;
        for (let i = 0; i < values.length; ++i) {
            series.append(offset + i, values[i]);
        }
    }

    Connections {
        target: panel.monitor
        function onReadingChanged() {
            panel.fill(edgeLine, panel.monitor.edgeHistory);
            panel.fill(hotspotLine, panel.monitor.hotspotHistory);
            panel.fill(powerLine, panel.monitor.powerHistory);
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        visible: panel.monitor.status === GpuMonitor.AmdCard && !panel.monitor.asleep
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: "Temperature, last minute (dashed: hotspot)"
            font: Kirigami.Theme.smallFont
        }
        Chart {
            low: panel.monitor.temperatureLow
            high: panel.monitor.temperatureHigh

            LineSeries {
                id: edgeLine
                color: Kirigami.Theme.highlightColor
                width: 2
            }
            LineSeries {
                id: hotspotLine
                color: Kirigami.Theme.highlightColor
                width: 2
                lineStyle: LineSeries.LineStyle.Straight
                strokeStyle: LineSeries.StrokeStyle.DashLine
                dashPattern: [3, 2]
            }
        }

        QQC2.Label {
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: "Power, last minute"
            font: Kirigami.Theme.smallFont
        }
        Chart {
            low: 0
            high: panel.monitor.powerHigh

            LineSeries {
                id: powerLine
                color: Kirigami.Theme.highlightColor
                width: 2
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
