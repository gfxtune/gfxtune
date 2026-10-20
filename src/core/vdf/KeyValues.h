// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QString>
#include <QStringView>

#include <initializer_list>
#include <vector>

namespace gfxtune::vdf
{

// One entry of Valve's KeyValues format: either a value or a section of
// further entries. Valve looks keys up without regard to case, and Steam's
// own files are not consistent about it ("apps" and "Apps" both occur), so
// lookups here ignore case too. Duplicate keys are kept in file order.
struct Node {
    QString key;
    QString value;
    std::vector<Node> children;
    bool isSection = false;

    // First child with this key, or nullptr.
    const Node *child(QStringView name) const;

    // Follows a chain of child keys, or nullptr if any link is missing.
    const Node *find(std::initializer_list<QStringView> path) const;

    // Value of the first child with this key; empty if there is none or it
    // is a section.
    QString valueOf(QStringView name) const;
};

} // namespace gfxtune::vdf
