// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QTextStream>

namespace gfxtune
{

// `gfxtune --list-games`: prints what gfxtune reads from Steam, for checking
// it on a real system and for bug reports. Read-only.
int listGames(QTextStream &out);

} // namespace gfxtune
