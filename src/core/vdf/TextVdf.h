// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "KeyValues.h"
#include "shared/Sizes.h"

#include <QByteArrayView>
#include <QString>

namespace gfxtune::vdf
{

// Parses Valve's text KeyValues format, as used by Steam's .vdf and .acf
// files. On success the top-level entries become the children of `root`.
// Platform conditionals such as [$WIN32] are accepted and ignored.
bool parseText(QByteArrayView data, Node &root, QString *error = nullptr);

// Reads and parses a file. Files larger than `maxBytes` are refused rather
// than loaded, so a damaged or unexpected file cannot exhaust memory.
bool readTextFile(const QString &path, Node &root, QString *error = nullptr, qint64 maxBytes = 64 * MiB);

} // namespace gfxtune::vdf
