// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "vdf/KeyValues.h"

#include <QString>
#include <QStringList>

namespace gfxtune::steam
{

// The portrait cover files a game's Steam data names, relative to the game's
// folder in Steam's library cache, in every language and size Steam lists
// (common/library_assets_full/library_capsule). Newer games name a file
// under a content hash, such as "<hash>/library_capsule.jpg". Names that
// would leave the game's folder are dropped.
QStringList listedCovers(const vdf::Node *appInfo);

// The portrait cover Steam shows for a game, read where Steam keeps it.
// In order: a cover the user picked in Steam, a file the game's Steam data
// names, any cover-named file in the game's cache folder or one level deeper
// under a content hash, then the older flat layout. Steam caches only the
// language and size it shows, so when several files exist the one it saved
// last wins. Empty when Steam has none cached; nothing is ever downloaded.
QString findCover(const QString &steamRoot, quint32 accountId, quint32 appId, const QStringList &listed = {});

} // namespace gfxtune::steam
