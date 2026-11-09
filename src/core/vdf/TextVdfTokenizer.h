// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

// The tokenizer behind TextVdf.cpp and TextVdfEdit.cpp. Internal to vdf/.

#include <KLocalizedString>
#include <QByteArray>
#include <QByteArrayView>
#include <QString>

namespace gfxtune::vdf::detail
{

enum class TokenType { String, Open, Close, Conditional, End, Invalid };

struct Token {
    TokenType type = TokenType::End;
    QByteArray text;
    // Byte range in the input, quotes included.
    qsizetype start = 0;
    qsizetype end = 0;
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
        const qsizetype start = m_pos;
        Token token = read();
        token.start = start;
        token.end = m_pos;
        return token;
    }

    qsizetype line() const { return m_line; }
    qsizetype position() const { return m_pos; }
    const QString &problem() const { return m_problem; }

private:
    Token read()
    {
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
        m_problem = i18n("text in quotes that starts on line %1 never ends", startLine);
        return {TokenType::Invalid, {}};
    }

    Token conditional()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_data.size() && m_data[m_pos] != ']' && m_data[m_pos] != '\n') {
            ++m_pos;
        }
        if (m_pos >= m_data.size() || m_data[m_pos] != ']') {
            m_problem = i18n("unclosed [ on line %1", m_line);
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
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v' || c == '"' || c == '{' || c == '}') {
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

} // namespace gfxtune::vdf::detail
