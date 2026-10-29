// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "cpu/Cpu.h"
#include "gpu/AmdGpu.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration>

namespace gfxtune
{

// The card panel's live readings. Reads only while `active` (the window is
// visible), once a second, and keeps the last minute for the graphs.
class GpuMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(int status READ status NOTIFY cardsChanged)
    Q_PROPERTY(QStringList cardNames READ cardNames NOTIFY cardsChanged)
    Q_PROPERTY(int currentCard READ currentCard WRITE setCurrentCard NOTIFY cardsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

    Q_PROPERTY(bool asleep READ asleep NOTIFY readingChanged)
    Q_PROPERTY(QString temperature READ temperature NOTIFY readingChanged)
    Q_PROPERTY(QString hotspot READ hotspot NOTIFY readingChanged)
    Q_PROPERTY(QString memoryTemperature READ memoryTemperature NOTIFY readingChanged)
    Q_PROPERTY(QString power READ power NOTIFY readingChanged)
    Q_PROPERTY(QString load READ load NOTIFY readingChanged)
    Q_PROPERTY(QString memoryLoad READ memoryLoad NOTIFY readingChanged)
    Q_PROPERTY(QString videoMemory READ videoMemory NOTIFY readingChanged)
    Q_PROPERTY(QString coreClock READ coreClock NOTIFY readingChanged)
    Q_PROPERTY(QString memoryClock READ memoryClock NOTIFY readingChanged)
    Q_PROPERTY(QString fan READ fan NOTIFY readingChanged)
    Q_PROPERTY(QString voltage READ voltage NOTIFY readingChanged)
    Q_PROPERTY(QString heldBackBy READ heldBackBy NOTIFY readingChanged)

    Q_PROPERTY(QString cpuLoad READ cpuLoad NOTIFY readingChanged)
    Q_PROPERTY(QString cpuTemperature READ cpuTemperature NOTIFY readingChanged)
    Q_PROPERTY(QString cpuClock READ cpuClock NOTIFY readingChanged)

    // The game being measured right now; 0 when none.
    Q_PROPERTY(int gameAppId READ gameAppId NOTIFY readingChanged)
    Q_PROPERTY(QString gameFps READ gameFps NOTIFY readingChanged)
    Q_PROPERTY(QString gameFrameTime READ gameFrameTime NOTIFY readingChanged)
    Q_PROPERTY(QString gameVideoMemory READ gameVideoMemory NOTIFY readingChanged)
    Q_PROPERTY(QList<qreal> fpsHistory READ fpsHistory NOTIFY readingChanged)
    Q_PROPERTY(qreal fpsHigh READ fpsHigh NOTIFY readingChanged)

    Q_PROPERTY(QList<qreal> edgeHistory READ edgeHistory NOTIFY readingChanged)
    Q_PROPERTY(QList<qreal> hotspotHistory READ hotspotHistory NOTIFY readingChanged)
    Q_PROPERTY(QList<qreal> powerHistory READ powerHistory NOTIFY readingChanged)
    Q_PROPERTY(qreal temperatureLow READ temperatureLow NOTIFY readingChanged)
    Q_PROPERTY(qreal temperatureHigh READ temperatureHigh NOTIFY readingChanged)
    Q_PROPERTY(qreal powerHigh READ powerHigh NOTIFY readingChanged)
    Q_PROPERTY(int historyLength READ historyLength CONSTANT)

public:
    enum Status {
        AmdCard,     // readings available
        OtherCards,  // only cards from other vendors
        NoCard,
    };
    Q_ENUM(Status)

    static constexpr int HistoryLength = 60;

    explicit GpuMonitor(QString sysRoot = QStringLiteral("/sys"), QObject *parent = nullptr, int intervalMs = 1000);

    int status() const;
    QStringList cardNames() const;
    int currentCard() const { return m_current; }
    void setCurrentCard(int index);
    bool active() const { return m_active; }
    void setActive(bool active);

    bool asleep() const { return m_reading.asleep; }
    QString temperature() const;
    QString hotspot() const;
    QString memoryTemperature() const;
    QString power() const;
    QString load() const;
    QString memoryLoad() const;
    QString videoMemory() const;
    QString coreClock() const;
    QString memoryClock() const;
    QString fan() const;
    QString voltage() const;
    QString heldBackBy() const;
    QString cpuLoad() const { return m_cpuLoad ? QStringLiteral("%1 %").arg(*m_cpuLoad) : QStringLiteral("—"); }
    QString cpuTemperature() const;
    QString cpuClock() const;

    int gameAppId() const { return int(m_gameAppId); }
    QString gameFps() const;
    QString gameFrameTime() const;
    QString gameVideoMemory() const;
    QList<qreal> fpsHistory() const { return m_fps; }
    qreal fpsHigh() const;

    // Where to look for a game being measured (the layer's live files under
    // <gfxtune folder>/performance) and for its processes.
    void setGameSources(QString performanceBase, QString procRoot = QStringLiteral("/proc"));

    QList<qreal> edgeHistory() const { return m_edge; }
    QList<qreal> hotspotHistory() const { return m_hotspot; }
    QList<qreal> powerHistory() const { return m_power; }
    qreal temperatureLow() const;
    qreal temperatureHigh() const;
    qreal powerHigh() const;
    int historyLength() const { return HistoryLength; }

    Q_INVOKABLE void rescan();
    // Takes one reading now; the timer calls this.
    void sample();
    // A pause longer than this starts the graphs afresh (5 s; shorter in tests).
    void setGapMs(qint64 ms) { m_gapMs = ms; }

Q_SIGNALS:
    void cardsChanged();
    void activeChanged();
    void readingChanged();

private:
    void clearHistory();
    void sampleGame();

    QString m_sysRoot;
    gpu::GpuScan m_scan;
    int m_current = 0;
    bool m_active = false;
    QTimer m_timer;
    QElapsedTimer m_sinceLastSample;
    qint64 m_gapMs = 5000;

    gpu::GpuReading m_reading;
    QList<qreal> m_edge;
    QList<qreal> m_hotspot;
    QList<qreal> m_power;

    std::optional<cpu::CpuTimes> m_cpuTimes;
    std::optional<int> m_cpuLoad;
    std::optional<double> m_cpuTemperature;
    std::optional<int> m_cpuClock;

    QString m_performanceBase;
    QString m_procRoot = QStringLiteral("/proc");
    quint32 m_gameAppId = 0;
    double m_gameFps = 0;
    double m_gameFrameMs = 0;
    double m_gameFrameMsMax = 0;
    std::optional<quint64> m_gameVideoMemory;
    int m_videoMemoryCountdown = 0;
    QList<qreal> m_fps;
};

} // namespace gfxtune
