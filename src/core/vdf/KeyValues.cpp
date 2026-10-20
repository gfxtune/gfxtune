// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "KeyValues.h"

namespace gfxtune::vdf
{

const Node *Node::child(QStringView name) const
{
    for (const Node &entry : children) {
        if (QStringView(entry.key).compare(name, Qt::CaseInsensitive) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

const Node *Node::find(std::initializer_list<QStringView> path) const
{
    const Node *node = this;
    for (QStringView name : path) {
        node = node->child(name);
        if (!node) {
            return nullptr;
        }
    }
    return node;
}

QString Node::valueOf(QStringView name) const
{
    const Node *entry = child(name);
    return entry && !entry->isSection ? entry->value : QString();
}

} // namespace gfxtune::vdf
