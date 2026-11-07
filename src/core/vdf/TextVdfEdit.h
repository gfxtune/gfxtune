// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "KeyValues.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gfxtune::vdf
{

// One value to set or remove in a text VDF file.
struct ValueEdit {
    QStringList sectionPath; // matched without regard to case, as Steam does
    QString key;
    std::optional<QString> value; // no value removes the key
};

// Changes only the bytes of those values; every other byte of the file
// stays as it was. Missing sections are created. A later edit of the same
// value replaces an earlier one. Returns false, leaving `data` untouched, if
// the file cannot be read as VDF. One pass over the file for all edits.
bool editText(QByteArray &data, const QList<ValueEdit> &edits, QString *error = nullptr);
bool editText(QByteArray &data, const ValueEdit &edit, QString *error = nullptr);

// The same edit on a parsed tree, used to check the result of editText().
void applyEdit(Node &root, const ValueEdit &edit);
bool sameTree(const Node &a, const Node &b);

// Applies edits to a file atomically. The new contents are parsed back and
// must equal the old contents with exactly these edits, or nothing is
// written. The file's permissions are kept.
bool editTextFile(const QString &path, const QList<ValueEdit> &edits, QString *error = nullptr);

} // namespace gfxtune::vdf
