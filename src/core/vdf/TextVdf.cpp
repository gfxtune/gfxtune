// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "TextVdf.h"

#include <QFile>

namespace gfxtune::vdf
{

namespace
{

constexpr int MaxDepth = 128;

enum class TokenType { String, Open, Close, Conditional, End, Invalid };

struct Token {
    TokenType type = TokenType::End;
    QByteArray text;
};

class Tokenizer
{
public:
    explicit Tokenizer(QByteArrayView data)
        : m_data(data)
    {
        if (m_data.startsWith("\xEF\xBB\xBF")) {
            m_pos = 3;
        }
    }

    Token next()
    {
        skipSpaceAndComments();
        if (m_pos >= m_data.size()) {
            return {TokenType::End, {}};
        }
        const char c = m_data[m_pos];
        if (c == '{') {
            ++m_pos;
            return {TokenType::Open, {}};
        }
        if (c == '}') {
            ++m_pos;
            return {TokenType::Close, {}};
        }
        if (c == '"') {
            return quoted();
        }
        if (c == '[') {
            return conditional();
        }
        return unquoted();
    }

    qsizetype line() const { return m_line; }
    const QString &problem() const { return m_problem; }

private:
    void skipSpaceAndComments()
    {
        while (m_pos < m_data.size()) {
            const char c = m_data[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '/') {
                while (m_pos < m_data.size() && m_data[m_pos] != '\n') {
                    ++m_pos;
                }
            } else {
                break;
            }
        }
    }

    Token quoted()
    {
        const qsizetype startLine = m_line;
        ++m_pos; // opening quote
        QByteArray text;
        while (m_pos < m_data.size()) {
            const char c = m_data[m_pos++];
            if (c == '"') {
                return {TokenType::String, text};
            }
            if (c == '\n') {
                ++m_line;
            }
            if (c == '\\' && m_pos < m_data.size()) {
                const char escaped = m_data[m_pos];
                switch (escaped) {
                case 'n':
                    text += '\n';
                    ++m_pos;
                    continue;
                case 't':
                    text += '\t';
                    ++m_pos;
                    continue;
                case '\\':
                case '"':
                    text += escaped;
                    ++m_pos;
                    continue;
                default:
                    break; // keep the backslash as written
                }
            }
            text += c;
        }
        m_problem = QStringLiteral("text in quotes that starts on line %1 never ends").arg(startLine);
        return {TokenType::Invalid, {}};
    }

    Token conditional()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_data.size() && m_data[m_pos] != ']' && m_data[m_pos] != '\n') {
            ++m_pos;
        }
        if (m_pos >= m_data.size() || m_data[m_pos] != ']') {
            m_problem = QStringLiteral("unclosed [ on line %1").arg(m_line);
            return {TokenType::Invalid, {}};
        }
        ++m_pos;
        return {TokenType::Conditional, m_data.sliced(start, m_pos - start).toByteArray()};
    }

    Token unquoted()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_data.size()) {
            const char c = m_data[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v' || c == '"' || c == '{'
                || c == '}') {
                break;
            }
            ++m_pos;
        }
        return {TokenType::String, m_data.sliced(start, m_pos - start).toByteArray()};
    }

    QByteArrayView m_data;
    qsizetype m_pos = 0;
    qsizetype m_line = 1;
    QString m_problem;
};

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

bool parseText(QByteArrayView data, Node &root, QString *error)
{
    root = Node{};
    root.isSection = true;

    Tokenizer tokens(data);
    std::vector<Node *> open{&root};

    // Conditionals may follow keys and values; they carry no data we use.
    auto nextToken = [&tokens]() {
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
        return fail(error, QStringLiteral("file is larger than %1 MB").arg(maxBytes / (1024 * 1024)));
    }
    const QByteArray data = file.readAll();
    QString problem;
    if (!parseText(data, root, &problem)) {
        return fail(error, problem);
    }
    return true;
}

} // namespace gfxtune::vdf
