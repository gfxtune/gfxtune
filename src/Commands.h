// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QTextStream>

namespace gfxtune
{

// `gfxtune --list-games`: prints what gfxtune reads from Steam, for checking
// it on a real system and for bug reports. Read-only.
int listGames(QTextStream &out);

// `gfxtune --remove-launch-lines`: takes gfxtune-run out of every game's
// Steam launch options. Run when gfxtune is removed; Steam must be closed.
int removeLaunchLines(QTextStream &out);

} // namespace gfxtune
