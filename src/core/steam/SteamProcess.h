// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QString>

namespace gfxtune::steam
{

// Whether this user's Steam client is running. Steam rewrites its settings
// files from memory when it exits, so they may only be edited while it is
// closed.
bool isSteamRunning(const QString &procRoot = QStringLiteral("/proc"));
bool isSteamRunning(const QString &procRoot, uint userId);

} // namespace gfxtune::steam
