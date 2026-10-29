// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Performance.h"

#include "shared/FrameHistogram.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace gfxtune::performance
{

namespace
{

const QString HistoryFile = QStringLiteral("history");

// "key=value" lines, as the layer writes them.
QHash<QString, QString> readKeys(const QString &path, qint64 maxSize)
{
    QHash<QString, QString> keys;
    QFile file(path);
    if (file.size() > maxSize || !file.open(QIODevice::ReadOnly)) {
        return keys;
    }
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (!line.startsWith(QLatin1Char('#')) && equals > 0) {
            keys.insert(line.left(equals), line.mid(equals + 1));
        }
    }
    return keys;
}

double number(const QHash<QString, QString> &keys, const QString &key)
{
    bool ok = false;
    const double value = keys.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : 0.0;
}

QString historyLine(const Session &s)
{
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11")
        .arg(s.start.toSecsSinceEpoch())
        .arg(s.seconds, 0, 'f', 1)
        .arg(s.frames)
        .arg(s.averageFps, 0, 'f', 2)
        .arg(s.low1Fps, 0, 'f', 2)
        .arg(s.low01Fps, 0, 'f', 2)
        .arg(s.edge, 0, 'f', 1)
        .arg(s.hotspot, 0, 'f', 1)
        .arg(s.hotspotMax, 0, 'f', 1)
        .arg(s.power, 0, 'f', 1)
        .arg(s.load, 0, 'f', 1);
}

std::optional<Session> parseHistoryLine(const QString &line)
{
    const QStringList fields = line.split(QLatin1Char(','));
    if (fields.size() != 11) {
        return std::nullopt;
    }
    bool ok = true;
    const auto field = [&fields, &ok](int i) {
        bool fieldOk = false;
        const double value = fields.at(i).toDouble(&fieldOk);
        ok = ok && fieldOk && std::isfinite(value) && value >= 0;
        return value;
    };
    Session s;
    s.start = QDateTime::fromSecsSinceEpoch(qint64(field(0)));
    s.seconds = field(1);
    s.frames = quint64(field(2));
    s.averageFps = field(3);
    s.low1Fps = field(4);
    s.low01Fps = field(5);
    s.edge = field(6);
    s.hotspot = field(7);
    s.hotspotMax = field(8);
    s.power = field(9);
    s.load = field(10);
    return ok ? std::optional<Session>(s) : std::nullopt;
}

std::optional<Session> sessionFrom(const QHash<QString, QString> &keys, QDateTime *updated)
{
    shared::FrameHistogram histogram;
    if (!keys.contains(QStringLiteral("start")) || !histogram.parse(keys.value(QStringLiteral("histogram")).toStdString())) {
        return std::nullopt;
    }
    const double start = number(keys, QStringLiteral("start"));
    if (start <= 0) {
        return std::nullopt;
    }
    Session s;
    s.start = QDateTime::fromSecsSinceEpoch(qint64(start));
    if (updated) {
        *updated = QDateTime::fromSecsSinceEpoch(qint64(number(keys, QStringLiteral("updated"))));
    }
    s.seconds = histogram.seconds();
    s.frames = histogram.frames();
    s.averageFps = histogram.averageFps();
    s.low1Fps = histogram.lowFps(1.0);
    s.low01Fps = histogram.lowFps(0.1);
    // A damaged reading counts as not taken, like the layer's 0.
    const double samples = number(keys, QStringLiteral("gpu_samples"));
    if (samples > 0) {
        const auto average = [&keys, samples](const QString &key) { return std::max(number(keys, key) / samples, 0.0); };
        s.edge = average(QStringLiteral("edge_sum"));
        s.hotspot = average(QStringLiteral("hotspot_sum"));
        s.power = average(QStringLiteral("power_sum"));
        s.load = average(QStringLiteral("load_sum"));
        s.hotspotMax = std::max(number(keys, QStringLiteral("hotspot_max")), 0.0);
    }
    return s;
}

// The process id in a session file's name, "session-<pid>". Other names
// are the layer's half-written "session-<pid>.tmp<pid>", or the leftover of
// a write that failed, which goes once it is a minute old.
std::optional<qint64> sessionPid(const QString &dir, const QString &name, const QDateTime &now)
{
    bool ok = false;
    const qint64 pid = name.mid(8).toLongLong(&ok);
    if (ok && pid > 0) {
        return pid;
    }
    const QString path = dir + QLatin1Char('/') + name;
    if (QFileInfo(path).lastModified().secsTo(now) > 60) {
        QFile::remove(path);
    }
    return std::nullopt;
}

// A session of a game that is still running: its process lives and the
// layer rewrote the file lately (every five seconds while the game runs).
bool stillWritten(qint64 pid, const QDateTime &updated, const std::function<bool(qint64)> &processAlive, const QDateTime &now)
{
    return processAlive(pid) && updated.isValid() && updated.secsTo(now) <= 30;
}

} // namespace

QString performanceRoot(const QString &baseDir)
{
    return baseDir + QStringLiteral("/performance");
}

QString performanceDir(const QString &baseDir, quint32 appId)
{
    return performanceRoot(baseDir) + QStringLiteral("/%1").arg(appId);
}

std::optional<Session> readSessionFile(const QString &path)
{
    return sessionFrom(readKeys(path, 256 * 1024), nullptr);
}

QList<Session> loadHistory(const QString &dir, const std::function<bool(qint64)> &processAlive, int keep, const QDateTime &now)
{
    QList<Session> history;
    QFile file(dir + QLatin1Char('/') + HistoryFile);
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : file.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            if (const auto session = parseHistoryLine(line)) {
                history.append(*session);
            }
        }
        file.close();
    }

    QList<Session> running;
    bool folded = false;
    const QStringList sessions = QDir(dir).entryList({QStringLiteral("session-*")}, QDir::Files);
    for (const QString &name : sessions) {
        const auto pid = sessionPid(dir, name, now);
        if (!pid) {
            continue;
        }
        const QString path = dir + QLatin1Char('/') + name;
        QDateTime updated;
        const auto session = sessionFrom(readKeys(path, 256 * 1024), &updated);
        if (stillWritten(*pid, updated, processAlive, now)) {
            if (session && session->seconds >= MinimumSeconds) {
                Session s = *session;
                s.running = true;
                running.append(s);
            }
            continue;
        }
        if (session && session->seconds >= MinimumSeconds) {
            history.append(*session);
        }
        QFile::remove(path);
        folded = true;
    }

    const auto newestFirst = [](const Session &a, const Session &b) { return a.start > b.start; };
    std::sort(running.begin(), running.end(), newestFirst);
    std::sort(history.begin(), history.end(), newestFirst);
    if (history.size() > keep) {
        history.resize(keep);
        folded = true;
    }
    if (folded) {
        QSaveFile out(dir + QLatin1Char('/') + HistoryFile);
        if (out.open(QIODevice::WriteOnly)) {
            out.write("# start,seconds,frames,average_fps,low_1,low_0.1,edge,hotspot,hotspot_max,power,load\n");
            for (const Session &s : std::as_const(history)) {
                out.write(historyLine(s).toUtf8() + '\n');
            }
            out.commit();
        }
    }
    return running + history;
}

bool clearHistory(const QString &dir, const std::function<bool(qint64)> &processAlive, const QDateTime &now)
{
    bool ok = !QFile::exists(dir + QLatin1Char('/') + HistoryFile) || QFile::remove(dir + QLatin1Char('/') + HistoryFile);
    const QStringList sessions = QDir(dir).entryList({QStringLiteral("session-*")}, QDir::Files);
    for (const QString &name : sessions) {
        const auto pid = sessionPid(dir, name, now);
        if (!pid) {
            continue;
        }
        const QString path = dir + QLatin1Char('/') + name;
        QDateTime updated;
        sessionFrom(readKeys(path, 256 * 1024), &updated);
        if (!stillWritten(*pid, updated, processAlive, now)) {
            ok = QFile::remove(path) && ok;
        }
    }
    return ok;
}

std::optional<Live> readLive(const QString &dir, const QDateTime &now)
{
    const QHash<QString, QString> keys = readKeys(dir + QStringLiteral("/live"), 4096);
    if (!keys.contains(QStringLiteral("time"))) {
        return std::nullopt;
    }
    Live live;
    live.time = QDateTime::fromSecsSinceEpoch(qint64(number(keys, QStringLiteral("time"))));
    // Written every second; older means the game stopped or froze.
    if (std::abs(live.time.secsTo(now)) > 3) {
        return std::nullopt;
    }
    live.fps = number(keys, QStringLiteral("fps"));
    live.frameMs = number(keys, QStringLiteral("frame_ms"));
    live.frameMsMax = number(keys, QStringLiteral("frame_ms_max"));
    return live;
}

} // namespace gfxtune::performance
