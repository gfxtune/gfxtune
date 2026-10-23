// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "steam/RunningGames.h"
#include "steam/SteamWatcher.h"
#include "support/Fixtures.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;

namespace
{

void fakeProcess(const QString &proc, const QString &pid, const QByteArray &comm, const QList<QByteArray> &args)
{
    test::writeFile(proc + QLatin1Char('/') + pid + QStringLiteral("/comm"), comm + '\n');
    test::writeFile(proc + QLatin1Char('/') + pid + QStringLiteral("/cmdline"), args.join('\0') + '\0');
}

} // namespace

class RunningTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void findsGamesStartedBySteam()
    {
        QTemporaryDir dir;
        const QString proc = dir.path();
        const QByteArray reaper = "/home/user/.local/share/Steam/ubuntu12_32/reaper";
        fakeProcess(proc, QStringLiteral("100"), "reaper", {reaper, "SteamLaunch", "AppId=1001", "--", "/x/proton", "waitforexitandrun", "game.exe"});
        fakeProcess(proc, QStringLiteral("101"), "reaper", {reaper, "SteamLaunch", "AppId=1002", "--", "./game"});
        // A reaper that is not launching a game.
        fakeProcess(proc, QStringLiteral("200"), "reaper", {reaper, "something", "AppId=2000"});
        // Not a reaper, even though its arguments look like one.
        fakeProcess(proc, QStringLiteral("300"), "game.exe", {"game.exe", "SteamLaunch", "AppId=3000"});
        // The id only appears in the game's own arguments.
        fakeProcess(proc, QStringLiteral("400"), "reaper", {reaper, "SteamLaunch", "--", "./game", "AppId=4000"});
        // Damaged ids.
        fakeProcess(proc, QStringLiteral("500"), "reaper", {reaper, "SteamLaunch", "AppId=abc"});
        fakeProcess(proc, QStringLiteral("501"), "reaper", {reaper, "SteamLaunch", "AppId=0"});
        // A process that ended between listing and reading.
        QDir().mkpath(proc + QStringLiteral("/600"));
        // Non-process folders.
        fakeProcess(proc, QStringLiteral("self"), "reaper", {reaper, "SteamLaunch", "AppId=7000"});

        QCOMPARE(steam::runningSteamApps(proc), (QSet<quint32>{1001, 1002}));
    }

    void handlesAMissingProcFolder()
    {
        QCOMPARE(steam::runningSteamApps(QStringLiteral("/does/not/exist")), QSet<quint32>());
    }

    void readsTheRealProcWithoutErrors()
    {
        // A game may well be running where the tests are built (the package
        // runs them), so this only checks that reading the real /proc works.
        const QSet<quint32> running = steam::runningSteamApps();
        QVERIFY(!running.contains(0));
    }

    void watcherReportsABurstOfChangesOnce()
    {
        QTemporaryDir dir;
        steam::SteamWatcher watcher(nullptr, 200);
        watcher.setPaths({dir.path()});
        QSignalSpy spy(&watcher, &steam::SteamWatcher::changed);

        for (int i = 0; i < 5; ++i) {
            test::writeFile(dir.filePath(QStringLiteral("appmanifest_%1.acf").arg(i)), "x");
        }
        QVERIFY(spy.wait(3000));
        QTest::qWait(500);
        QCOMPARE(spy.count(), 1);
    }

    void watcherFollowsReplacedPaths()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        steam::SteamWatcher watcher(nullptr, 100);
        watcher.setPaths({first.path()});
        watcher.setPaths({second.path()});
        QCOMPARE(watcher.paths(), QStringList{second.path()});
        QSignalSpy spy(&watcher, &steam::SteamWatcher::changed);

        test::writeFile(first.filePath(QStringLiteral("ignored")), "x");
        QVERIFY(!spy.wait(600));
        test::writeFile(second.filePath(QStringLiteral("seen")), "x");
        QVERIFY(spy.wait(3000));

        watcher.setPaths({});
        QVERIFY(watcher.paths().isEmpty());
    }
};

QTEST_GUILESS_MAIN(RunningTest)
#include "tst_running.moc"
