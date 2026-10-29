// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "performance/Performance.h"
#include "shared/FrameHistogram.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace gfxtune;
using namespace gfxtune::performance;

namespace
{

const QDateTime Now = QDateTime::fromSecsSinceEpoch(1'790'000'000);

// What the layer writes for one session: `seconds` of frames at `fps`.
QByteArray sessionText(qint64 start, qint64 updated, double seconds, double fps, int samples = 10)
{
    shared::FrameHistogram histogram;
    const auto frameNs = std::int64_t(1e9 / fps);
    for (std::int64_t drawn = 0; drawn < std::int64_t(seconds * 1e9); drawn += frameNs) {
        histogram.add(frameNs);
    }
    return "# Written by gfxtune's Vulkan layer: one play session.\n"
           "start=" + QByteArray::number(start) + "\nupdated=" + QByteArray::number(updated)
         + "\nhistogram=" + QByteArray::fromStdString(histogram.serialize())
         + "\ngpu_samples=" + QByteArray::number(samples) + "\nedge_sum=" + QByteArray::number(samples * 60.5)
         + "\nhotspot_sum=" + QByteArray::number(samples * 75.0) + "\nhotspot_max=88\npower_sum="
         + QByteArray::number(samples * 210.0) + "\nload_sum=" + QByteArray::number(samples * 97.0) + "\n";
}

void writeSession(const QString &dir, qint64 pid, const QByteArray &text)
{
    QVERIFY(test::writeFile(dir + QStringLiteral("/session-%1").arg(pid), text));
}

QStringList sessionFiles(const QString &dir)
{
    return QDir(dir).entryList({QStringLiteral("session-*")}, QDir::Files, QDir::Name);
}

QList<qint64> starts(const QList<Session> &sessions)
{
    QList<qint64> out;
    for (const Session &s : sessions) {
        out.append(s.start.toSecsSinceEpoch());
    }
    return out;
}

} // namespace

class PerformanceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void namesTheFolders()
    {
        QCOMPARE(performanceRoot(QStringLiteral("/base")), QStringLiteral("/base/performance"));
        QCOMPARE(performanceDir(QStringLiteral("/base"), 1091500), QStringLiteral("/base/performance/1091500"));
    }

    void readsASessionFile()
    {
        QTemporaryDir dir;
        const qint64 start = Now.toSecsSinceEpoch() - 3600;
        writeSession(dir.path(), 42, sessionText(start, start + 600, 600, 120));
        const auto s = readSessionFile(dir.filePath(QStringLiteral("session-42")));
        QVERIFY(s);
        QCOMPARE(s->start.toSecsSinceEpoch(), start);
        QVERIFY(std::abs(s->seconds - 600) < 0.1);
        QVERIFY(std::abs(s->averageFps - 120) < 1);
        // All frames equal: the lows are the average.
        QVERIFY(std::abs(s->low1Fps - 120) < 2);
        QVERIFY(std::abs(s->low01Fps - 120) < 2);
        QCOMPARE(s->edge, 60.5);
        QCOMPARE(s->hotspot, 75.0);
        QCOMPARE(s->hotspotMax, 88.0);
        QCOMPARE(s->power, 210.0);
        QCOMPARE(s->load, 97.0);
        QVERIFY(!s->running);

        // Without the card's readings (no amdgpu card) they stay at 0.
        writeSession(dir.path(), 43, sessionText(start, start, 60, 60, 0));
        const auto blind = readSessionFile(dir.filePath(QStringLiteral("session-43")));
        QVERIFY(blind);
        QCOMPARE(blind->edge, 0.0);
        QCOMPARE(blind->power, 0.0);
        QCOMPARE(blind->hotspotMax, 0.0);

        QVERIFY(!readSessionFile(dir.filePath(QStringLiteral("missing"))));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("no-start")), "histogram=\n"));
        QVERIFY(!readSessionFile(dir.filePath(QStringLiteral("no-start"))));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("bad-histogram")), "start=1\nhistogram=1:2:3\n"));
        QVERIFY(!readSessionFile(dir.filePath(QStringLiteral("bad-histogram"))));
        QByteArray before1970 = sessionText(start, start, 60, 60);
        before1970.replace("start=" + QByteArray::number(start), "start=-5");
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("before-1970")), before1970));
        QVERIFY(!readSessionFile(dir.filePath(QStringLiteral("before-1970"))));

        // Damaged readings count as not taken.
        QByteArray negative = sessionText(start, start, 60, 60);
        negative.replace("hotspot_max=88", "hotspot_max=-88");
        negative.replace("power_sum=", "power_sum=-");
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("negative")), negative));
        const auto damaged = readSessionFile(dir.filePath(QStringLiteral("negative")));
        QVERIFY(damaged);
        QCOMPARE(damaged->hotspotMax, 0.0);
        QCOMPARE(damaged->power, 0.0);
        QCOMPARE(damaged->edge, 60.5);
    }

    // Finished sessions go into the history file; the one still being
    // written is listed first as running.
    void foldsFinishedSessionsIntoTheHistory()
    {
        QTemporaryDir dir;
        const qint64 now = Now.toSecsSinceEpoch();
        const QSet<qint64> alive{200, 300};
        const auto processAlive = [&alive](qint64 pid) { return alive.contains(pid); };

        writeSession(dir.path(), 100, sessionText(now - 7200, now - 3600, 3000, 90));   // ended
        writeSession(dir.path(), 200, sessionText(now - 600, now - 2, 590, 144));       // running
        writeSession(dir.path(), 300, sessionText(now - 9000, now - 8000, 900, 60));    // alive pid, but long silent: reused or hung
        writeSession(dir.path(), 400, sessionText(now - 500, now - 490, 10, 60));       // too short to keep
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("session-500")), "damaged"));
        // The layer's half-written file and an old leftover of a failed write.
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("session-200.tmp200")), sessionText(now - 600, now, 595, 144)));
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("session-9.tmp9")), "half"));
        QFile leftover(dir.filePath(QStringLiteral("session-9.tmp9")));
        QVERIFY(leftover.open(QIODevice::ReadWrite));
        QVERIFY(leftover.setFileTime(Now.addSecs(-120), QFileDevice::FileModificationTime));
        leftover.close();
        QFile fresh(dir.filePath(QStringLiteral("session-200.tmp200")));
        QVERIFY(fresh.open(QIODevice::ReadWrite));
        QVERIFY(fresh.setFileTime(Now, QFileDevice::FileModificationTime));
        fresh.close();

        const QList<Session> sessions = loadHistory(dir.path(), processAlive, 50, Now);
        QCOMPARE(starts(sessions), (QList<qint64>{now - 600, now - 7200, now - 9000}));
        QVERIFY(sessions[0].running);
        QVERIFY(!sessions[1].running);
        QVERIFY(!sessions[2].running);
        QCOMPARE(sessionFiles(dir.path()), (QStringList{QStringLiteral("session-200"), QStringLiteral("session-200.tmp200")}));

        // Read back from the history file alone, with the same values.
        const QList<Session> again = loadHistory(dir.path(), processAlive, 50, Now);
        QCOMPARE(again.size(), 3);
        for (int i = 1; i < 3; ++i) {
            QCOMPARE(again[i].start, sessions[i].start);
            QCOMPARE(again[i].frames, sessions[i].frames);
            QVERIFY(std::abs(again[i].averageFps - sessions[i].averageFps) < 0.01);
            QVERIFY(std::abs(again[i].low1Fps - sessions[i].low1Fps) < 0.01);
            QCOMPARE(again[i].edge, sessions[i].edge);
            QCOMPARE(again[i].hotspotMax, sessions[i].hotspotMax);
            QCOMPARE(again[i].power, sessions[i].power);
        }

        // The game ends: its session joins the history.
        const QList<Session> after = loadHistory(dir.path(), [](qint64) { return false; }, 50, Now);
        QCOMPARE(starts(after), (QList<qint64>{now - 600, now - 7200, now - 9000}));
        QVERIFY(!after[0].running);
    }

    void listsARunningSessionOnceItCounts()
    {
        QTemporaryDir dir;
        const qint64 now = Now.toSecsSinceEpoch();
        const auto alive = [](qint64) { return true; };
        writeSession(dir.path(), 10, sessionText(now - 15, now, 15, 60));
        QVERIFY(loadHistory(dir.path(), alive, 50, Now).isEmpty());
        writeSession(dir.path(), 10, sessionText(now - 25, now, 25, 60));
        QCOMPARE(loadHistory(dir.path(), alive, 50, Now).size(), 1);
    }

    void keepsTheNewestSessions()
    {
        QTemporaryDir dir;
        const qint64 now = Now.toSecsSinceEpoch();
        for (int i = 1; i <= 6; ++i) {
            writeSession(dir.path(), i, sessionText(now - i * 1000, now - i * 1000 + 100, 60, 60));
        }
        const auto dead = [](qint64) { return false; };
        QCOMPARE(starts(loadHistory(dir.path(), dead, 4, Now)), (QList<qint64>{now - 1000, now - 2000, now - 3000, now - 4000}));
        // The older ones are gone from the file too.
        QCOMPARE(loadHistory(dir.path(), dead, 50, Now).size(), 4);
    }

    void skipsDamagedHistoryLines()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("history")),
                                "# start,seconds,frames,average_fps,low_1,low_0.1,edge,hotspot,hotspot_max,power,load\n"
                                "1789990000,600.0,72000,120.00,95.00,80.00,60.0,75.0,88.0,210.0,97.0\n"
                                "1789980000,600.0,72000,120.00\n"                                  // too short
                                "1789970000,600.0,72000,120.00,95.00,80.00,60.0,75.0,88.0,210.0,x\n" // not a number
                                "1789960000,-600.0,72000,120.00,95.00,80.00,60.0,75.0,88.0,210.0,97.0\n" // negative
                                "1789950000,600.0,72000,inf,95.00,80.00,60.0,75.0,88.0,210.0,97.0\n"
                                "\n   \n"
                                "1789940000,300.0,18000,60.00,50.00,45.00,0.0,0.0,0.0,0.0,0.0\n"));
        const QList<Session> sessions = loadHistory(dir.path(), [](qint64) { return false; }, 50, Now);
        QCOMPARE(starts(sessions), (QList<qint64>{1789990000, 1789940000}));
        QCOMPARE(sessions[0].averageFps, 120.0);
        QCOMPARE(sessions[0].low01Fps, 80.0);
        QCOMPARE(sessions[1].power, 0.0);
    }

    void clearsAllButARunningSession()
    {
        QTemporaryDir dir;
        const qint64 now = Now.toSecsSinceEpoch();
        const auto alive = [](qint64 pid) { return pid == 2; };
        writeSession(dir.path(), 1, sessionText(now - 5000, now - 4000, 900, 60));
        writeSession(dir.path(), 2, sessionText(now - 100, now, 90, 60));
        QCOMPARE(loadHistory(dir.path(), alive, 50, Now).size(), 2);
        writeSession(dir.path(), 3, sessionText(now - 3000, now - 2000, 900, 60)); // ended, not folded yet

        QVERIFY(clearHistory(dir.path(), alive, Now));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("history"))));
        QCOMPARE(sessionFiles(dir.path()), QStringList{QStringLiteral("session-2")});
        const QList<Session> left = loadHistory(dir.path(), alive, 50, Now);
        QCOMPARE(starts(left), QList<qint64>{now - 100});
        QVERIFY(left[0].running);

        // Nothing there is fine too.
        QTemporaryDir empty;
        QVERIFY(clearHistory(empty.path(), alive, Now));
        QVERIFY(clearHistory(empty.filePath(QStringLiteral("never-measured")), alive, Now));
        QVERIFY(loadHistory(empty.filePath(QStringLiteral("never-measured")), alive, 50, Now).isEmpty());
    }

    void readsTheLiveFile()
    {
        QTemporaryDir dir;
        const auto write = [&dir](qint64 time, const QByteArray &fps) {
            QVERIFY(test::writeFile(dir.filePath(QStringLiteral("live")),
                                    "# Written by gfxtune's Vulkan layer while the game runs.\ntime=" + QByteArray::number(time)
                                        + "\nfps=" + fps + "\nframe_ms=8.33\nframe_ms_max=21.5\n"));
        };
        const qint64 now = Now.toSecsSinceEpoch();
        QVERIFY(!readLive(dir.path(), Now)); // no file
        write(now - 1, "120.4");
        const auto live = readLive(dir.path(), Now);
        QVERIFY(live);
        QCOMPARE(live->fps, 120.4);
        QCOMPARE(live->frameMs, 8.33);
        QCOMPARE(live->frameMsMax, 21.5);

        // Written every second: older means the game ended or hangs; far
        // ahead means the clock was changed.
        write(now - 3, "60");
        QVERIFY(readLive(dir.path(), Now));
        write(now - 4, "60");
        QVERIFY(!readLive(dir.path(), Now));
        write(now + 60, "60");
        QVERIFY(!readLive(dir.path(), Now));

        // Damaged numbers read as nothing measured.
        write(now, "nan");
        QCOMPARE(readLive(dir.path(), Now)->fps, 0.0);
        write(now, "fast");
        QCOMPARE(readLive(dir.path(), Now)->fps, 0.0);
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("live")), "fps=60\n"));
        QVERIFY(!readLive(dir.path(), Now));
        // Too big to be the layer's.
        QVERIFY(test::writeFile(dir.filePath(QStringLiteral("live")), "time=" + QByteArray::number(now) + "\n" + QByteArray(8192, '#')));
        QVERIFY(!readLive(dir.path(), Now));
    }

    // Damaged session and history files never crash and never give values
    // that make no sense.
    void survivesDamagedFiles()
    {
        std::mt19937 random(7);
        const qint64 now = Now.toSecsSinceEpoch();
        const QByteArray session = sessionText(now - 900, now - 800, 100, 75);
        const QByteArray history = "1789990000,600.0,72000,120.00,95.00,80.00,60.0,75.0,88.0,210.0,97.0\n";
        for (int round = 0; round < 300; ++round) {
            QTemporaryDir dir;
            writeSession(dir.path(), 5, test::mutate(session, random));
            QVERIFY(test::writeFile(dir.filePath(QStringLiteral("history")), test::mutate(history, random)));
            const QList<Session> first = loadHistory(dir.path(), [](qint64) { return false; }, 50, Now);
            // What was folded reads back the same from the history file.
            QCOMPARE(loadHistory(dir.path(), [](qint64) { return false; }, 50, Now).size(), first.size());
            for (const Session &s : first) {
                for (double v : {s.seconds, s.averageFps, s.low1Fps, s.low01Fps, s.edge, s.hotspot, s.hotspotMax, s.power, s.load}) {
                    QVERIFY(std::isfinite(v));
                    QVERIFY(v >= 0);
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(PerformanceTest)
#include "tst_performance.moc"
