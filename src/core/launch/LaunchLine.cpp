// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LaunchLine.h"

#include <QRegularExpression>
#include <QStringList>

namespace gfxtune::launch
{

namespace
{

const QLatin1StringView Command("%command%");

// Splits like a shell would: on spaces outside quotes, with quotes removed.
QStringList words(const QString &text)
{
    QStringList out;
    QString word;
    bool inWord = false;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < text.size()) {
                word += text[++i];
            } else {
                word += c;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            inWord = true;
        } else if (c == QLatin1Char('\\') && i + 1 < text.size()) {
            word += text[++i];
            inWord = true;
        } else if (c.isSpace()) {
            if (inWord) {
                out.append(word);
                word.clear();
                inWord = false;
            }
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord) {
        out.append(word);
    }
    return out;
}

const QRegularExpression &helperBeforeCommand()
{
    // Also matches a full path such as /usr/bin/gfxtune-run.
    static const QRegularExpression re(QStringLiteral("(?<!\\S)(?:\\S*/)?gfxtune-run\\s+(?=%command%)"));
    return re;
}

} // namespace

bool hasHelper(const QString &launchOptions)
{
    return helperBeforeCommand().match(launchOptions).hasMatch();
}

QString withHelper(const QString &launchOptions)
{
    if (hasHelper(launchOptions)) {
        return launchOptions;
    }
    const QString trimmed = launchOptions.trimmed();
    if (trimmed.isEmpty()) {
        return HelperName + QLatin1Char(' ') + Command;
    }
    const qsizetype at = launchOptions.indexOf(Command);
    if (at < 0) {
        // Plain game arguments: Steam appends them to the command.
        return HelperName + QLatin1Char(' ') + Command + QLatin1Char(' ') + trimmed;
    }
    QString out = launchOptions;
    out.insert(at, HelperName + QLatin1Char(' '));
    return out;
}

QString withoutHelper(const QString &launchOptions)
{
    QString out = launchOptions;
    out.remove(helperBeforeCommand());
    if (out.trimmed() == Command) {
        return {};
    }
    return out;
}

OwnSettings ownSettings(const QString &launchOptions)
{
    OwnSettings own;
    static const QRegularExpression assignment(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)="));
    for (const QString &word : words(launchOptions)) {
        if (word == Command) {
            break; // what follows belongs to the game
        }
        const QRegularExpressionMatch m = assignment.match(word);
        if (m.hasMatch()) {
            own.environment.insert(m.captured(1));
        } else {
            const QString program = word.section(QLatin1Char('/'), -1);
            own.gamescope = own.gamescope || program == QLatin1String("gamescope");
            own.gameMode = own.gameMode || program == QLatin1String("gamemoderun");
            own.mangoHud = own.mangoHud || program == QLatin1String("mangohud");
        }
    }
    return own;
}

} // namespace gfxtune::launch
