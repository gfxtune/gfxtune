// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "LaunchPlan.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace gfxtune::launch
{

// What gfxtune-run did the last time a game started, kept in
// <gfxtune folder>/last-launch/<app id>.ini for the game's page.
struct LastLaunch {
    QDateTime time;
    // In the user's language at the time, for showing.
    QStringList applied;
    QStringList keptFromSteam;
    QStringList notApplied;
    bool filters = false; // the in-game filters were asked for
    bool layer = false;   // gfxtune's Vulkan layer was asked for
};

// What gfxtune's Vulkan layer reported from inside the game once the game
// had its swapchain, kept in <gfxtune folder>/last-launch/<app id>.filters.
// The layer writes it without Qt: "key=value" lines, time in Unix seconds.
struct FilterReport {
    QDateTime time;
    bool running = false;
    QString detail; // the picture's size and format, or why it was skipped
};

QString lastLaunchPath(const QString &baseDir, quint32 appId);
QString filterReportPath(const QString &baseDir, quint32 appId);
std::optional<FilterReport> readFilterReport(const QString &baseDir, quint32 appId);
bool writeLastLaunch(const QString &baseDir, quint32 appId, const LaunchPlan &plan, const QDateTime &time);
std::optional<LastLaunch> readLastLaunch(const QString &baseDir, quint32 appId);

// Whether gfxtune's layer has seen the game show a Vulkan picture since
// `since`: it reported on the filters, or measured a frame.
bool sawVulkanPicture(const QString &baseDir, quint32 appId, const QDateTime &since);

// How long a game must run before gfxtune decides it draws without Vulkan.
inline constexpr qint64 LearningSeconds = 60;

// Whether the game that started at `gameStarted` showed no Vulkan picture,
// though gfxtune's layer was in it (`launch` is the record of that start)
// and it has run for a minute: it draws with OpenGL.
bool showedNoVulkanPicture(const LastLaunch &launch, const QDateTime &gameStarted, const QDateTime &now, bool sawVulkan);

} // namespace gfxtune::launch
