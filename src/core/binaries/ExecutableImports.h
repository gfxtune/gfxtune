// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringList>

namespace gfxtune::binaries
{

enum class ExecutableFormat { Unknown, Windows, Linux };

struct Imports {
    ExecutableFormat format = ExecutableFormat::Unknown;
    // Libraries the program asks the system to load: for Windows programs
    // the regular and delay-loaded imports, in lower case; for Linux
    // programs the DT_NEEDED entries as written.
    QStringList libraries;
};

// Reads the list of libraries from a Windows (PE) or Linux (ELF) program or
// library. Only headers and import tables are touched, never code.
bool parseImports(QByteArrayView data, Imports &out, QString *error = nullptr);
bool readImports(const QString &path, Imports &out, QString *error = nullptr);

} // namespace gfxtune::binaries
