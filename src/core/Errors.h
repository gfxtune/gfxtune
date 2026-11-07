// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QString>

namespace gfxtune
{

// For functions that say what went wrong through an optional QString and
// return false: `return fail(error, QStringLiteral("..."));`.
inline bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace gfxtune
