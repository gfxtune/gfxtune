// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GpuMonitor.h"

#include "performance/Performance.h"
#include "steam/RunningGames.h"

#include <QDir>
#include <QtMath>

#include <algorithm>

namespace gfxtune
{

namespace
{

const QString Missing = QStringLiteral("—");

template<typename T>
QString whole(const std::optional<T> &value, const QString &unit)
{
    return value ? QStringLiteral("%1 %2").arg(qRound(double(*value))).arg(unit) : Missing;
}

void push(QList<qreal> &history, const std::optional<double> &value)
{
    if (!value) {
        return;
    }
    history.append(*value);
    while (history.size() > GpuMonitor::HistoryLength) {
        history.removeFirst();
    }
}

} // namespace

GpuMonitor::GpuMonitor(QString sysRoot, QObject *parent, int intervalMs)
    : QObject(parent)
    , m_sysRoot(std::move(sysRoot))
{
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &GpuMonitor::sample);
    rescan();
}

int GpuMonitor::status() const
{
    if (!m_scan.amd.isEmpty()) {
        return AmdCard;
    }
    return m_scan.otherCards > 0 ? OtherCards : NoCard;
}

QStringList GpuMonitor::cardNames() const
{
    QStringList names;
    for (const gpu::AmdGpu &card : m_scan.amd) {
        names.append(card.name);
    }
    return names;
}

void GpuMonitor::setCurrentCard(int index)
{
    if (index < 0 || index >= m_scan.amd.size() || index == m_current) {
        return;
    }
    m_current = index;
    clearHistory();
    Q_EMIT cardsChanged();
    if (m_active) {
        sample();
    }
}

void GpuMonitor::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (active) {
        if (m_sinceLastSample.isValid() && m_sinceLastSample.elapsed() > m_gapMs) {
            clearHistory();
        }
        sample();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    Q_EMIT activeChanged();
}

void GpuMonitor::rescan()
{
    m_scan = gpu::findGpus(m_sysRoot);
    m_current = 0;
    clearHistory();
    Q_EMIT cardsChanged();
}

// The processor and the game being measured are read with any card.
void GpuMonitor::sample()
{
    // After a longer pause the old history would join up with the new
    // readings as if nothing happened in between, so it starts afresh.
    if (m_sinceLastSample.isValid() && m_sinceLastSample.elapsed() > m_gapMs) {
        clearHistory();
    }
    m_sinceLastSample.start();
    if (m_current < m_scan.amd.size()) {
        m_reading = gpu::readGpu(m_scan.amd.at(m_current));
        if (!m_reading.asleep) {
            push(m_edge, m_reading.edgeTemp);
            push(m_hotspot, m_reading.hotspotTemp);
            push(m_power, m_reading.power);
        }
    }

    const std::optional<cpu::CpuTimes> times = cpu::readCpuTimes(m_procRoot);
    m_cpuLoad = m_cpuTimes && times ? cpu::cpuLoad(*m_cpuTimes, *times) : std::nullopt;
    m_cpuTimes = times;
    m_cpuTemperature = cpu::cpuTemperature(m_sysRoot);
    m_cpuClock = cpu::cpuClock(m_sysRoot);

    sampleGame();
    Q_EMIT readingChanged();
}

void GpuMonitor::setGameSources(QString performanceBase, QString procRoot)
{
    m_performanceBase = std::move(performanceBase);
    m_procRoot = std::move(procRoot);
}

// The newest live file says which game is being measured.
void GpuMonitor::sampleGame()
{
    quint32 appId = 0;
    std::optional<performance::Live> newest;
    if (!m_performanceBase.isEmpty()) {
        const QStringList games = QDir(m_performanceBase).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &game : games) {
            bool ok = false;
            const quint32 id = game.toUInt(&ok);
            const auto live = ok ? performance::readLive(m_performanceBase + QLatin1Char('/') + game) : std::nullopt;
            if (live && (!newest || live->time > newest->time)) {
                newest = live;
                appId = id;
            }
        }
    }
    if (appId != m_gameAppId) {
        m_fps.clear();
        m_gameVideoMemory.reset();
        m_videoMemoryCountdown = 0;
    }
    m_gameAppId = appId;
    if (!newest) {
        return;
    }
    m_gameFps = newest->fps;
    m_gameFrameMs = newest->frameMs;
    m_gameFrameMsMax = newest->frameMsMax;
    push(m_fps, newest->fps);

    // Walking the game's processes costs a few milliseconds: every third second.
    if (m_videoMemoryCountdown-- <= 0) {
        m_videoMemoryCountdown = 2;
        const qint64 reaper = steam::steamGameReapers(m_procRoot).value(appId, 0);
        m_gameVideoMemory = reaper > 0 ? gpu::processVideoMemory(steam::processTree(reaper, m_procRoot), m_procRoot) : std::nullopt;
    }
}

void GpuMonitor::clearHistory()
{
    m_edge.clear();
    m_hotspot.clear();
    m_power.clear();
    m_fps.clear();
    m_reading = {};
    m_cpuTimes.reset(); // the load is worked out between two close readings
}

QString GpuMonitor::heldBackBy() const
{
    if (!m_reading.heldBackBy) {
        return Missing;
    }
    if (m_reading.heldBackBy->isEmpty()) {
        return QStringLiteral("Nothing");
    }
    QString words = m_reading.heldBackBy->join(QStringLiteral(", "));
    words[0] = words[0].toUpper();
    return words;
}

QString GpuMonitor::cpuTemperature() const
{
    return m_cpuTemperature ? QStringLiteral("%1 °C").arg(qRound(*m_cpuTemperature)) : Missing;
}

QString GpuMonitor::cpuClock() const
{
    return m_cpuClock ? QStringLiteral("%1 GHz").arg(double(*m_cpuClock) / 1000.0, 0, 'f', 2) : Missing;
}

QString GpuMonitor::gameFps() const
{
    return m_gameAppId != 0 ? QStringLiteral("%1 FPS").arg(qRound(m_gameFps)) : Missing;
}

QString GpuMonitor::gameFrameTime() const
{
    if (m_gameAppId == 0 || m_gameFrameMs <= 0) {
        return Missing;
    }
    return QStringLiteral("%1 ms · slowest %2 ms").arg(m_gameFrameMs, 0, 'f', 1).arg(m_gameFrameMsMax, 0, 'f', 1);
}

QString GpuMonitor::gameVideoMemory() const
{
    if (!m_gameVideoMemory) {
        return Missing;
    }
    constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
    return QStringLiteral("%1 GB").arg(double(*m_gameVideoMemory) / GiB, 0, 'f', 1);
}

// The frame rate graph reaches the fastest second, in steps of 30.
qreal GpuMonitor::fpsHigh() const
{
    qreal high = 60;
    for (qreal v : m_fps) {
        high = qMax(high, v);
    }
    return qCeil(high / 30) * 30;
}

QString GpuMonitor::temperature() const { return whole(m_reading.edgeTemp, QStringLiteral("°C")); }
QString GpuMonitor::hotspot() const { return whole(m_reading.hotspotTemp, QStringLiteral("°C")); }
QString GpuMonitor::memoryTemperature() const { return whole(m_reading.memoryTemp, QStringLiteral("°C")); }
QString GpuMonitor::load() const { return whole(m_reading.load, QStringLiteral("%")); }
QString GpuMonitor::memoryLoad() const { return whole(m_reading.memoryLoad, QStringLiteral("%")); }
QString GpuMonitor::coreClock() const { return whole(m_reading.coreClock, QStringLiteral("MHz")); }
QString GpuMonitor::memoryClock() const { return whole(m_reading.memoryClock, QStringLiteral("MHz")); }
QString GpuMonitor::voltage() const { return whole(m_reading.voltage, QStringLiteral("mV")); }

QString GpuMonitor::power() const
{
    if (!m_reading.power) {
        return Missing;
    }
    const QString now = QString::number(qRound(*m_reading.power));
    return m_reading.powerLimit ? QStringLiteral("%1 / %2 W").arg(now).arg(qRound(*m_reading.powerLimit)) : now + QStringLiteral(" W");
}

QString GpuMonitor::videoMemory() const
{
    if (!m_reading.vramUsed || !m_reading.vramTotal) {
        return Missing;
    }
    constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
    return QStringLiteral("%1 / %2 GB").arg(double(*m_reading.vramUsed) / GiB, 0, 'f', 1).arg(double(*m_reading.vramTotal) / GiB, 0, 'f', 1);
}

QString GpuMonitor::fan() const
{
    if (!m_reading.fanRpm && !m_reading.fanPercent) {
        return Missing;
    }
    if (m_reading.fanRpm && m_reading.fanPercent) {
        return QStringLiteral("%1 RPM · %2%").arg(*m_reading.fanRpm).arg(*m_reading.fanPercent);
    }
    return m_reading.fanRpm ? QStringLiteral("%1 RPM").arg(*m_reading.fanRpm) : QStringLiteral("%1%").arg(*m_reading.fanPercent);
}

// The temperature graph spans the readings with a margin, in steps of ten.
qreal GpuMonitor::temperatureLow() const
{
    qreal low = 100;
    for (const QList<qreal> *history : {&m_edge, &m_hotspot}) {
        for (qreal v : *history) {
            low = qMin(low, v);
        }
    }
    return qMax<qreal>(0, qFloor((low - 5) / 10) * 10);
}

qreal GpuMonitor::temperatureHigh() const
{
    qreal high = 50;
    for (const QList<qreal> *history : {&m_edge, &m_hotspot}) {
        for (qreal v : *history) {
            high = qMax(high, v);
        }
    }
    return qCeil((high + 5) / 10) * 10;
}

// The power graph reaches at least the power limit, in steps of 50 W.
qreal GpuMonitor::powerHigh() const
{
    qreal high = m_reading.powerLimit.value_or(100);
    for (qreal v : m_power) {
        high = qMax(high, v);
    }
    return qCeil(high / 50) * 50;
}

} // namespace gfxtune
