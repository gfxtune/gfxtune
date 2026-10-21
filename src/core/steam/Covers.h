// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QString>

namespace gfxtune::steam
{

// The portrait cover Steam shows for a game, read where Steam keeps it.
// In order: a cover the user picked in Steam, Steam's cache in its current
// layout (a folder per game, sometimes one level deeper under a content
// hash), then the older flat layout. Empty when Steam has none cached;
// nothing is ever downloaded.
QString findCover(const QString &steamRoot, quint32 accountId, quint32 appId);

} // namespace gfxtune::steam
