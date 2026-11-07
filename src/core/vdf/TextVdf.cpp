// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "TextVdf.h"

#include "Errors.h"
#include "TextVdfTokenizer.h"
#include "shared/Sizes.h"

#include <QFile>

namespace gfxtune::vdf
{

using detail::Token;
using detail::Tokenizer;
using detail::TokenType;

namespace
{

constexpr int MaxDepth = 128;

} // namespace

bool parseText(QByteArrayView data, Node &root, QString *error)
{
    root = Node{};
    root.isSection = true;

    Tokenizer tokens(data);
    std::vector<Node *> open{&root};

    // Conditionals may follow keys and values; they carry no data we use.
    const auto nextToken = [&tokens] {
        Token token = tokens.next();
        while (token.type == TokenType::Conditional) {
            token = tokens.next();
        }
        return token;
    };

    for (;;) {
        Token token = nextToken();
        switch (token.type) {
        case TokenType::Invalid:
            return fail(error, tokens.problem());
        case TokenType::End:
            if (open.size() > 1) {
                return fail(error, QStringLiteral("file ends inside a section (missing })"));
            }
            return true;
        case TokenType::Close:
            if (open.size() == 1) {
                return fail(error, QStringLiteral("unexpected } on line %1").arg(tokens.line()));
            }
            open.pop_back();
            continue;
        case TokenType::Open:
            return fail(error, QStringLiteral("{ without a name on line %1").arg(tokens.line()));
        case TokenType::Conditional:
        case TokenType::String:
            break;
        }

        Node entry;
        entry.key = QString::fromUtf8(token.text);

        Token value = nextToken();
        switch (value.type) {
        case TokenType::String:
            entry.value = QString::fromUtf8(value.text);
            open.back()->children.push_back(std::move(entry));
            break;
        case TokenType::Open:
            if (open.size() > MaxDepth) {
                return fail(error, QStringLiteral("sections nested too deeply on line %1").arg(tokens.line()));
            }
            entry.isSection = true;
            open.back()->children.push_back(std::move(entry));
            open.push_back(&open.back()->children.back());
            break;
        case TokenType::Invalid:
            return fail(error, tokens.problem());
        case TokenType::Close:
        case TokenType::End:
        case TokenType::Conditional:
            return fail(error, QStringLiteral("\"%1\" has no value (line %2)").arg(entry.key).arg(tokens.line()));
        }
    }
}

bool readTextFile(const QString &path, Node &root, QString *error, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, file.errorString());
    }
    if (file.size() > maxBytes) {
        return fail(error, QStringLiteral("file is larger than %1 MB").arg(maxBytes / MiB));
    }
    const QByteArray data = file.readAll();
    QString problem;
    if (!parseText(data, root, &problem)) {
        return fail(error, problem);
    }
    return true;
}

} // namespace gfxtune::vdf
