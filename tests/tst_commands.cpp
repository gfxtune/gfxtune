// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Runs the real gfxtune program's text-only options against a made-up Steam.

#include "support/FakeSteam.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;

class CommandsTest : public QObject
{
    Q_OBJECT

private:
    struct Result {
        int exitCode = -1;
        QString out;
    };

    static Result run(const QStringList &arguments, const QString &home, const QString &toolsPath = {})
    {
        QProcessEnvironment env;
        env.insert(QStringLiteral("HOME"), home);
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        if (!toolsPath.isEmpty()) {
            env.insert(QStringLiteral("STEAM_EXTRA_COMPAT_TOOLS_PATHS"), toolsPath);
        }
        // No display at all: these options must not need one.
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(GFXTUNE_PATH), arguments);
        if (!process.waitForFinished(20000)) {
            process.kill();
            return {};
        }
        return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
                QString::fromUtf8(process.readAllStandardOutput())};
    }

    // The lines printed for one game, from its heading to the blank line.
    static QString gameBlock(const QString &out, const QString &heading)
    {
        const qsizetype start = out.indexOf(QLatin1Char('\n') + heading + QLatin1Char('\n'));
        if (start < 0) {
            return {};
        }
        const qsizetype end = out.indexOf(QStringLiteral("\n\n"), start + 1);
        return out.mid(start + 1, end < 0 ? -1 : end - start - 1);
    }

private Q_SLOTS:
    void listsGames()
    {
        test::FakeSteam steam;
        steam.build();
        const Result result = run({QStringLiteral("--list-games")}, steam.home, steam.systemTools);
        QCOMPARE(result.exitCode, 0);
        QVERIFY2(result.out.contains(QLatin1String("Signed-in account: found")), qPrintable(result.out));

        const QString neon = gameBlock(result.out, QStringLiteral("1002  Neon Drift"));
        QVERIFY2(!neon.isEmpty(), qPrintable(result.out));
        QVERIFY(neon.contains(QLatin1String("runs as:   Proton (Proton 9.0)")));
        QVERIFY(neon.contains(QLatin1String("graphics:  DirectX 11 (from its files)")));
        QVERIFY(neon.contains(QLatin1String("cover:     found")));

        const QString ashfall = gameBlock(result.out, QStringLiteral("1001  Ashfall Protocol"));
        QVERIFY2(!ashfall.isEmpty(), qPrintable(result.out));
        QVERIFY(ashfall.contains(QLatin1String("cover:     none in Steam's cache")));
        QVERIFY(ashfall.contains(QLatin1String("launch options: gamemoderun %command% -dx12 \"quoted arg\"")));

        QVERIFY(result.out.contains(QLatin1String("Installed but not listed")));
        QVERIFY(result.out.contains(QLatin1String("not a game (Steam type: Music)")));
    }

    void reportsMissingSteam()
    {
        QTemporaryDir home;
        const Result result = run({QStringLiteral("--list-games")}, home.path());
        QCOMPARE(result.exitCode, 1);
        QVERIFY2(result.out.contains(QLatin1String("Steam was not found")), qPrintable(result.out));
    }

    void printsVersionAndHelpWithoutADisplay()
    {
        QTemporaryDir home;
        Result result = run({QStringLiteral("--version")}, home.path());
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.out.startsWith(QLatin1String("gfxtune ")));
        result = run({QStringLiteral("--help")}, home.path());
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.out.contains(QLatin1String("--list-games")));
        QVERIFY(result.out.contains(QLatin1String("--remove-launch-lines")));
    }
};

QTEST_GUILESS_MAIN(CommandsTest)
#include "tst_commands.moc"
