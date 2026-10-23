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
    QStringList applied;
    QStringList keptFromSteam;
    QStringList notApplied;
};

QString lastLaunchPath(const QString &baseDir, quint32 appId);
bool writeLastLaunch(const QString &baseDir, quint32 appId, const LaunchPlan &plan, const QDateTime &time);
std::optional<LastLaunch> readLastLaunch(const QString &baseDir, quint32 appId);

} // namespace gfxtune::launch
