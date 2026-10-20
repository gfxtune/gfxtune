// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "KeyValues.h"

#include <QByteArrayView>
#include <QString>
#include <QStringList>

namespace gfxtune::vdf
{

// Parses Valve's binary KeyValues format, as stored per app in Steam's
// appinfo.vdf. Numbers are converted to their decimal text so every value
// reads the same way as in the text format.
//
// Newer appinfo.vdf files (version 29) store keys as indexes into a table at
// the end of the file instead of inline text; pass that table as `keyTable`.
bool parseBinary(QByteArrayView data, Node &root, QString *error = nullptr, const QStringList *keyTable = nullptr);

} // namespace gfxtune::vdf
