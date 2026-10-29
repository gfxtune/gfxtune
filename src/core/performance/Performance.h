// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace gfxtune::performance
{

// <gfxtune folder>/performance/<app id>: where the layer writes what it
// measures in a game (see src/layer/Recorder.h), and where the app keeps the
// game's history.
QString performanceRoot(const QString &baseDir);
QString performanceDir(const QString &baseDir, quint32 appId);

// One play session, summed up.
struct Session {
    QDateTime start;
    double seconds = 0; // time spent drawing, without pauses
    quint64 frames = 0;
    double averageFps = 0;
    double low1Fps = 0;  // the frame rate at the 99th percentile of frame times
    double low01Fps = 0; // ... and at the 99.9th
    // The card while the game drew; 0 when it could not be read.
    double edge = 0;
    double hotspot = 0;
    double hotspotMax = 0;
    double power = 0;
    double load = 0;
    bool running = false; // still being written

    bool operator==(const Session &) const = default;
};

// Shorter sessions are a launcher or a game that closed at once.
constexpr double MinimumSeconds = 20.0;

// A session file the layer wrote; nullopt when it is not one.
std::optional<Session> readSessionFile(const QString &path);

// The game's sessions, newest first: finished session files are folded into
// the history file (keeping the newest `keep`), the one still being written
// is listed as running. `processAlive` tells whether a process still runs;
// a session not updated for a while is finished whatever it says.
// Running sessions shorter than MinimumSeconds are left out until they are not.
QList<Session> loadHistory(const QString &dir, const std::function<bool(qint64)> &processAlive, int keep = 50,
                           const QDateTime &now = QDateTime::currentDateTime());
// Removes the history and finished session files; a session still being
// written stays.
bool clearHistory(const QString &dir, const std::function<bool(qint64)> &processAlive,
                  const QDateTime &now = QDateTime::currentDateTime());

// The last second of a game that is being measured, when fresh.
struct Live {
    QDateTime time;
    double fps = 0;
    double frameMs = 0;
    double frameMsMax = 0;
};
std::optional<Live> readLive(const QString &dir, const QDateTime &now = QDateTime::currentDateTime());

} // namespace gfxtune::performance
