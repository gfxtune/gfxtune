// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "TextVdfEdit.h"

#include "Errors.h"
#include "TextVdf.h"
#include "TextVdfTokenizer.h"
#include "shared/Sizes.h"

#include <KLocalizedString>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <optional>
#include <utility>

namespace gfxtune::vdf
{

using detail::Token;
using detail::Tokenizer;
using detail::TokenType;

namespace
{

constexpr qint64 MaxFileBytes = 64 * MiB;

struct SectionSpan {
    QStringList path;
    qsizetype openEnd = 0; // just after its {
};

struct ValueSpan {
    QStringList path; // of the section holding it
    QString key;
    qsizetype keyStart = 0;
    qsizetype valueStart = 0;
    qsizetype valueEnd = 0;
};

struct Index {
    QList<SectionSpan> sections;
    QList<ValueSpan> values;
};

bool buildIndex(QByteArrayView data, Index &index, QString *error)
{
    Tokenizer tokens(data);
    QStringList path;
    const auto next = [&tokens] {
        Token t = tokens.next();
        while (t.type == TokenType::Conditional) {
            t = tokens.next();
        }
        return t;
    };
    for (;;) {
        const Token key = next();
        switch (key.type) {
        case TokenType::End:
            return path.isEmpty() || fail(error, i18n("file ends inside a section"));
        case TokenType::Invalid:
            return fail(error, tokens.problem());
        case TokenType::Close:
            if (path.isEmpty()) {
                return fail(error, i18n("unexpected } on line %1", tokens.line()));
            }
            path.removeLast();
            continue;
        case TokenType::Open:
            return fail(error, i18n("{ without a name on line %1", tokens.line()));
        case TokenType::Conditional:
        case TokenType::String:
            break;
        }
        const QString name = QString::fromUtf8(key.text);
        const Token value = next();
        if (value.type == TokenType::String) {
            index.values.append({path, name, key.start, value.start, value.end});
        } else if (value.type == TokenType::Open) {
            path.append(name);
            index.sections.append({path, value.end});
        } else if (value.type == TokenType::Invalid) {
            return fail(error, tokens.problem());
        } else {
            return fail(error, i18n("\"%1\" has no value (line %2)", name, tokens.line()));
        }
    }
}

QByteArray quoted(const QString &text)
{
    QByteArray out("\"");
    for (const char c : text.toUtf8()) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out + '"';
}

QByteArray tabs(qsizetype depth)
{
    return QByteArray(depth, '\t');
}

// The text for sections path[from..] holding key=value, in Steam's layout,
// to go right after an opening brace at depth `from`.
QByteArray newSections(const QStringList &path, qsizetype from, const QString &key, const QString &value)
{
    QByteArray text;
    for (qsizetype depth = from; depth < path.size(); ++depth) {
        text += '\n' + tabs(depth) + quoted(path[depth]) + '\n' + tabs(depth) + '{';
    }
    text += '\n' + tabs(path.size()) + quoted(key) + "\t\t" + quoted(value);
    for (qsizetype depth = path.size() - 1; depth >= from; --depth) {
        text += '\n' + tabs(depth) + '}';
    }
    return text;
}

bool onlySpace(QByteArrayView text)
{
    for (const char c : text) {
        if (c != ' ' && c != '\t' && c != '\r') {
            return false;
        }
    }
    return true;
}

// Paths and keys as Steam matches them: without regard to case.
QString pathKey(const QStringList &path)
{
    return path.join(QChar(0x1f)).toCaseFolded();
}

QString valueKey(const QStringList &path, const QString &key)
{
    return pathKey(path) + QChar(0x1e) + key.toCaseFolded();
}

// One change to the file's bytes: `length` bytes at `at` become `text`.
struct Change {
    qsizetype at = 0;
    qsizetype length = 0;
    QByteArray text;
};

// Where each value and section of an index is, by path, first one first.
struct Lookup {
    explicit Lookup(const Index &index)
    {
        for (qsizetype i = 0; i < index.values.size(); ++i) {
            values.insert(valueKey(index.values[i].path, index.values[i].key), i); // keeps the first
        }
        for (const SectionSpan &section : index.sections) {
            sections.insert(pathKey(section.path), section.openEnd);
        }
    }
    QHash<QString, qsizetype> values;   // into Index::values
    QHash<QString, qsizetype> sections; // just after the section's {
};

Change removal(const QByteArray &data, const ValueSpan &v)
{
    // The whole line when the pair has it to itself.
    const qsizetype lineStart = data.lastIndexOf('\n', v.keyStart - 1) + 1;
    qsizetype lineEnd = data.indexOf('\n', v.valueEnd);
    lineEnd = lineEnd < 0 ? data.size() : lineEnd + 1;
    const QByteArrayView before = QByteArrayView(data).sliced(lineStart, v.keyStart - lineStart);
    const QByteArrayView after = QByteArrayView(data).sliced(v.valueEnd, lineEnd - v.valueEnd);
    if (onlySpace(before) && onlySpace(QByteArrayView(after).chopped(after.endsWith('\n') ? 1 : 0))) {
        return {lineStart, lineEnd - lineStart, {}};
    }
    return {v.keyStart, v.valueEnd - v.keyStart, {}};
}

// The deepest section of the edit's path that exists, and how many of the
// path's sections that is (0: none, the edit goes at the end of the file).
std::pair<qsizetype, std::optional<qsizetype>> deepestSection(const Lookup &lookup, const QStringList &path)
{
    for (qsizetype depth = path.size(); depth > 0; --depth) {
        const auto found = lookup.sections.constFind(pathKey(path.first(depth)));
        if (found != lookup.sections.constEnd()) {
            return {depth, *found};
        }
    }
    return {0, std::nullopt};
}

Change addition(const QByteArray &data, const ValueEdit &edit, const QString &value, qsizetype depth, std::optional<qsizetype> openEnd)
{
    if (openEnd) {
        return {*openEnd, 0, newSections(edit.sectionPath, depth, edit.key, value)};
    }
    // Not even the first section exists: add everything at the end.
    QByteArray text = newSections(edit.sectionPath, 0, edit.key, value);
    text.remove(0, 1); // no leading newline at the start of a line
    if (!data.isEmpty() && !data.endsWith('\n')) {
        text.prepend('\n');
    }
    return {data.size(), 0, text + '\n'};
}

// The data with every change made, in one pass. Changes at the same place
// go in reverse order, as if each had been made right after the one before.
QByteArray withChanges(const QByteArray &data, QList<Change> changes)
{
    std::ranges::stable_sort(changes, [](const Change &a, const Change &b) { return a.at < b.at; });
    QByteArray out;
    qsizetype grown = 0;
    for (const Change &change : changes) {
        grown += change.text.size() - change.length;
    }
    out.reserve(data.size() + qMax<qsizetype>(grown, 0));
    qsizetype copied = 0;
    for (qsizetype i = 0; i < changes.size();) {
        const qsizetype at = changes[i].at;
        out.append(QByteArrayView(data).sliced(copied, at - copied));
        qsizetype j = i;
        while (j < changes.size() && changes[j].at == at) {
            ++j;
        }
        qsizetype removed = 0;
        for (qsizetype k = j - 1; k >= i; --k) {
            out.append(changes[k].text);
            removed = qMax(removed, changes[k].length);
        }
        copied = at + removed;
        i = j;
    }
    out.append(QByteArrayView(data).sliced(copied));
    return out;
}

// One edit per value: a later edit of the same value replaces an earlier
// one, in the earlier one's place.
QList<ValueEdit> merged(const QList<ValueEdit> &edits)
{
    QList<ValueEdit> out;
    QHash<QString, qsizetype> places;
    for (const ValueEdit &edit : edits) {
        const QString key = valueKey(edit.sectionPath, edit.key);
        const auto place = places.constFind(key);
        if (place != places.constEnd()) {
            out[*place] = edit;
        } else {
            places.insert(key, out.size());
            out.append(edit);
        }
    }
    return out;
}

} // namespace

bool editText(QByteArray &data, const QList<ValueEdit> &edits, QString *error)
{
    // The result is the same as making the edits one at a time, in order.
    // An edit that needs a section an earlier one creates waits for the next
    // pass over the file; that happens only where sections are missing.
    const QList<ValueEdit> all = merged(edits);
    QByteArray changed = data;
    qsizetype next = 0;
    while (next < all.size()) {
        Index index;
        if (!buildIndex(changed, index, error)) {
            return false;
        }
        const Lookup lookup(index);
        QList<Change> changes;
        QSet<QString> creating; // sections this pass creates
        bool appending = false; // this pass adds at the end of the file
        for (; next < all.size(); ++next) {
            const ValueEdit &edit = all[next];
            const auto found = lookup.values.constFind(valueKey(edit.sectionPath, edit.key));
            if (found != lookup.values.constEnd()) {
                const ValueSpan &v = index.values[*found];
                changes.append(edit.value ? Change{v.valueStart, v.valueEnd - v.valueStart, quoted(*edit.value)} : removal(changed, v));
                continue;
            }
            if (!edit.value) {
                continue; // nothing to remove
            }
            const auto [depth, openEnd] = deepestSection(lookup, edit.sectionPath);
            const QString firstNew = depth < edit.sectionPath.size() ? pathKey(edit.sectionPath.first(depth + 1)) : QString();
            if ((!firstNew.isEmpty() && creating.contains(firstNew)) || (depth == 0 && appending)) {
                break;
            }
            if (!firstNew.isEmpty()) {
                creating.insert(firstNew);
            }
            appending = appending || depth == 0;
            changes.append(addition(changed, edit, *edit.value, depth, openEnd));
        }
        changed = withChanges(changed, changes);
    }
    data = std::move(changed);
    return true;
}

bool editText(QByteArray &data, const ValueEdit &edit, QString *error)
{
    return editText(data, QList<ValueEdit>{edit}, error);
}

void applyEdit(Node &root, const ValueEdit &edit)
{
    Node *section = &root;
    for (const QString &name : edit.sectionPath) {
        Node *found = nullptr;
        for (Node &child : section->children) {
            if (child.isSection && child.key.compare(name, Qt::CaseInsensitive) == 0) {
                found = &child;
                break;
            }
        }
        if (!found) {
            if (!edit.value) {
                return;
            }
            Node created;
            created.key = name;
            created.isSection = true;
            // editText() puts new sections first, right after the brace, and
            // new top-level ones at the end of the file.
            if (section == &root) {
                section->children.push_back(std::move(created));
                found = &section->children.back();
            } else {
                section->children.insert(section->children.begin(), std::move(created));
                found = &section->children.front();
            }
        }
        section = found;
    }
    for (auto it = section->children.begin(); it != section->children.end(); ++it) {
        if (!it->isSection && it->key.compare(edit.key, Qt::CaseInsensitive) == 0) {
            if (edit.value) {
                it->value = *edit.value;
            } else {
                section->children.erase(it);
            }
            return;
        }
    }
    if (edit.value) {
        Node entry;
        entry.key = edit.key;
        entry.value = *edit.value;
        section->children.insert(section->children.begin(), std::move(entry));
    }
}

bool sameTree(const Node &a, const Node &b)
{
    if (a.key != b.key || a.value != b.value || a.isSection != b.isSection || a.children.size() != b.children.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!sameTree(a.children[i], b.children[i])) {
            return false;
        }
    }
    return true;
}

bool editTextFile(const QString &path, const QList<ValueEdit> &edits, QString *error)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return fail(error, in.errorString());
    }
    if (in.size() > MaxFileBytes) {
        return fail(error, i18n("file is unexpectedly large"));
    }
    const QByteArray original = in.readAll();
    const QFileDevice::Permissions permissions = in.permissions();
    in.close();

    Node expected;
    if (!parseText(original, expected, error)) {
        return false;
    }
    QByteArray changed = original;
    if (!editText(changed, edits, error)) {
        return false;
    }
    for (const ValueEdit &edit : merged(edits)) {
        applyEdit(expected, edit);
    }
    if (changed == original) {
        return true;
    }

    Node actual;
    QString problem;
    if (!parseText(changed, actual, &problem) || !sameTree(expected, actual)) {
        return fail(error, i18n("the edited file did not read back as expected; nothing was written"));
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(changed) != changed.size() || !out.commit()) {
        return fail(error, out.errorString());
    }
    QFile::setPermissions(path, permissions);
    return true;
}

} // namespace gfxtune::vdf
