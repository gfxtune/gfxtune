// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "TextVdfEdit.h"

#include "TextVdf.h"
#include "TextVdfTokenizer.h"

#include <QFile>
#include <QSaveFile>

namespace gfxtune::vdf
{

using detail::Token;
using detail::TokenType;
using detail::Tokenizer;

namespace
{

constexpr qint64 MaxFileBytes = qint64(64) * 1024 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

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

bool samePath(const QStringList &a, const QStringList &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (a[i].compare(b[i], Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

bool buildIndex(QByteArrayView data, Index &index, QString *error)
{
    Tokenizer tokens(data);
    QStringList path;
    auto next = [&tokens] {
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
            return path.isEmpty() || fail(error, QStringLiteral("file ends inside a section"));
        case TokenType::Invalid:
            return fail(error, tokens.problem());
        case TokenType::Close:
            if (path.isEmpty()) {
                return fail(error, QStringLiteral("unexpected } on line %1").arg(tokens.line()));
            }
            path.removeLast();
            continue;
        case TokenType::Open:
            return fail(error, QStringLiteral("{ without a name on line %1").arg(tokens.line()));
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
            return fail(error, QStringLiteral("\"%1\" has no value (line %2)").arg(name).arg(tokens.line()));
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

} // namespace

bool editText(QByteArray &data, const ValueEdit &edit, QString *error)
{
    Index index;
    if (!buildIndex(data, index, error)) {
        return false;
    }

    for (const ValueSpan &v : std::as_const(index.values)) {
        if (!samePath(v.path, edit.sectionPath) || v.key.compare(edit.key, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (edit.value) {
            data.replace(v.valueStart, v.valueEnd - v.valueStart, quoted(*edit.value));
            return true;
        }
        // Remove the whole line when the pair has it to itself.
        const qsizetype lineStart = data.lastIndexOf('\n', v.keyStart - 1) + 1;
        qsizetype lineEnd = data.indexOf('\n', v.valueEnd);
        lineEnd = lineEnd < 0 ? data.size() : lineEnd + 1;
        const QByteArrayView before = QByteArrayView(data).sliced(lineStart, v.keyStart - lineStart);
        const QByteArrayView after = QByteArrayView(data).sliced(v.valueEnd, lineEnd - v.valueEnd);
        if (onlySpace(before) && onlySpace(QByteArrayView(after).chopped(after.endsWith('\n') ? 1 : 0))) {
            data.remove(lineStart, lineEnd - lineStart);
        } else {
            data.remove(v.keyStart, v.valueEnd - v.keyStart);
        }
        return true;
    }

    if (!edit.value) {
        return true; // nothing to remove
    }

    // Add it to the deepest section of the path that exists, creating the rest.
    qsizetype depth = edit.sectionPath.size();
    for (; depth > 0; --depth) {
        const QStringList prefix = edit.sectionPath.first(depth);
        for (const SectionSpan &s : std::as_const(index.sections)) {
            if (samePath(s.path, prefix)) {
                data.insert(s.openEnd, newSections(edit.sectionPath, depth, edit.key, *edit.value));
                return true;
            }
        }
    }
    // Not even the first section exists: add everything at the top.
    QByteArray text = newSections(edit.sectionPath, 0, edit.key, *edit.value);
    text.remove(0, 1); // no leading newline at the start of a line
    if (!data.isEmpty() && !data.endsWith('\n')) {
        data += '\n';
    }
    data += text + '\n';
    return true;
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
            // editText() puts new sections first, right after the brace.
            section->children.insert(section->children.begin(), std::move(created));
            found = &section->children.front();
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
        return fail(error, QStringLiteral("file is unexpectedly large"));
    }
    const QByteArray original = in.readAll();
    const QFileDevice::Permissions permissions = in.permissions();
    in.close();

    Node expected;
    if (!parseText(original, expected, error)) {
        return false;
    }
    QByteArray changed = original;
    for (const ValueEdit &edit : edits) {
        if (!editText(changed, edit, error)) {
            return false;
        }
        applyEdit(expected, edit);
    }
    if (changed == original) {
        return true;
    }

    Node actual;
    QString problem;
    if (!parseText(changed, actual, &problem) || !sameTree(expected, actual)) {
        return fail(error, QStringLiteral("the edited file did not read back as expected; nothing was written"));
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(changed) != changed.size() || !out.commit()) {
        return fail(error, out.errorString());
    }
    QFile::setPermissions(path, permissions);
    return true;
}

} // namespace gfxtune::vdf
