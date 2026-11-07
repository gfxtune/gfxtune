// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GpuMonitor.h"
#include "cpu/Cpu.h"
#include "gpu/AmdGpu.h"
#include "support/FakeSysfs.h"
#include "support/Fixtures.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using namespace gfxtune::gpu;
using test::FakeCard;

namespace
{

// gpu_metrics version 1.3 (120 bytes) with these throttling bits set.
QByteArray metricsWith(quint64 bits, quint8 revision = 3)
{
    QByteArray metrics(120, '\0');
    metrics[0] = char(120);
    metrics[2] = 1;
    metrics[3] = char(revision);
    for (int i = 0; i < 8; ++i) {
        metrics[112 + i] = char((bits >> (8 * i)) & 0xff);
    }
    return metrics;
}

constexpr quint64 bit(int n)
{
    return quint64(1) << n;
}

// One open file of a process, as /proc/<pid>/fdinfo/<fd> shows it.
void fakeFdinfo(const QString &proc, int pid, int fd, const QByteArray &text)
{
    test::writeFile(proc + QStringLiteral("/%1/fdinfo/%2").arg(pid).arg(fd), text);
}

QByteArray amdgpuClient(int client, const QByteArray &vram, const QByteArray &key = "drm-resident-vram")
{
    return "pos:\t0\nflags:\t02100002\nmnt_id:\t25\ndrm-driver:\tamdgpu\ndrm-client-id:\t" + QByteArray::number(client)
        + "\ndrm-memory-gtt:\t2048 KiB\n" + key + ":\t" + vram + "\n";
}

// A hwmon sensor under <sys>/class/hwmon.
void fakeSensor(const QString &sys, int index, const QByteArray &name, const QList<std::pair<QByteArray, int>> &temps)
{
    const QString dir = sys + QStringLiteral("/class/hwmon/hwmon%1/").arg(index);
    test::writeFile(dir + QStringLiteral("name"), name + '\n');
    int n = 1;
    for (const auto &[label, milli] : temps) {
        if (!label.isEmpty()) {
            test::writeFile(dir + QStringLiteral("temp%1_label").arg(n), label + '\n');
        }
        test::writeFile(dir + QStringLiteral("temp%1_input").arg(n), QByteArray::number(milli) + '\n');
        ++n;
    }
}

} // namespace

class GpuTest : public QObject
{
    Q_OBJECT

private:
    // A 7900 XT, a processor's built-in graphics and an NVIDIA card.
    void threeCards(const QString &root)
    {
        test::addCard(root, 0, FakeCard{.device = QStringLiteral("0x15bf"), .revision = QStringLiteral("0xc2"), .vramTotal = 536870912});
        test::addCard(root, 1);
        test::addCard(root, 2, FakeCard{.vendor = QStringLiteral("0x10de"), .device = QStringLiteral("0x2504")});
    }

private Q_SLOTS:
    void findsAndOrdersCards()
    {
        QTemporaryDir sys;
        QTemporaryDir data;
        threeCards(sys.path());
        const GpuScan scan = findGpus(sys.path(), {test::writeNameTable(data.path())});
        QCOMPARE(scan.amd.size(), 2);
        QCOMPARE(scan.otherCards, 1);
        QCOMPARE(scan.amd[0].name, QStringLiteral("AMD Radeon RX 7900 XT")); // the strongest first
        QCOMPARE(scan.amd[0].deviceId, quint16(0x744c));
        QCOMPARE(scan.amd[0].revision, quint8(0xcc));
        QCOMPARE(scan.amd[1].name, QStringLiteral("AMD Radeon 780M Graphics"));
        QVERIFY(scan.amd[0].hwmonPath.endsWith(QLatin1String("/hwmon/hwmon1")));
    }

    void namesCards()
    {
        QTemporaryDir data;
        const QStringList table{test::writeNameTable(data.path())};
        QCOMPARE(marketingName(0x744c, 0xc8, table), QStringLiteral("AMD Radeon RX 7900 XTX"));
        QCOMPARE(marketingName(0x13c0, 0xc1, table), QStringLiteral("AMD Radeon Graphics, with a comma"));
        QCOMPARE(marketingName(0x744c, 0xff, table), QStringLiteral("AMD graphics card 744C:FF"));
        QCOMPARE(marketingName(0x744c, 0xcc, {QStringLiteral("/does/not/exist")}), QStringLiteral("AMD graphics card 744C:CC"));
    }

    void handlesNoCardsAtAll()
    {
        QTemporaryDir sys;
        QVERIFY(findGpus(sys.path()).amd.isEmpty());
        QVERIFY(findGpus(QStringLiteral("/does/not/exist")).amd.isEmpty());
    }

    void readsEverySensor()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        const GpuReading r = readGpu(findGpus(sys.path()).amd.first());
        QVERIFY(!r.asleep);
        QCOMPARE(r.edgeTemp, 41.0);
        QCOMPARE(r.hotspotTemp, 46.0);
        QCOMPARE(r.memoryTemp, 52.0);
        QCOMPARE(r.power, 21.0);
        QCOMPARE(r.powerLimit, 315.0);
        QCOMPARE(r.load, 3);
        QCOMPARE(r.memoryLoad, 1);
        QCOMPARE(r.vramUsed, quint64(1073741824));
        QCOMPARE(r.vramTotal, quint64(21458059264ull));
        QCOMPARE(r.coreClock, 28);
        QCOMPARE(r.memoryClock, 96);
        QCOMPARE(r.fanRpm, 0);
        QCOMPARE(r.fanPercent, 0);
        QCOMPARE(r.voltage, 632);
    }

    void matchesTemperaturesByLabel()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/temp1_label"), "junction");
        test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/temp2_label"), "edge");
        const GpuReading r = readGpu(findGpus(sys.path()).amd.first());
        QCOMPARE(r.hotspotTemp, 41.0);
        QCOMPARE(r.edgeTemp, 46.0);
    }

    void fallsBackToInstantPower()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        QVERIFY(QFile::remove(sys.filePath(QStringLiteral("class/drm/card0/device/hwmon/hwmon0/power1_average"))));
        test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/power1_input"), "254500000");
        QCOMPARE(readGpu(findGpus(sys.path()).amd.first()).power, 254.5);
    }

    void convertsFanSpeed()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        for (const auto &[pwm, percent] : QList<std::pair<QByteArray, int>>{{"255", 100}, {"128", 50}, {"1", 0}}) {
            test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/pwm1"), pwm);
            QCOMPARE(readGpu(findGpus(sys.path()).amd.first()).fanPercent, percent);
        }
        test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/pwm1"), "300");
        QVERIFY(!readGpu(findGpus(sys.path()).amd.first()).fanPercent);
    }

    void neverWakesASleepingCard()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0, FakeCard{.runtimeStatus = QStringLiteral("suspended")});
        const GpuReading r = readGpu(findGpus(sys.path()).amd.first());
        QVERIFY(r.asleep);
        QVERIFY(!r.edgeTemp && !r.power && !r.load && !r.vramUsed);
    }

    void toleratesMissingAndDamagedFiles()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0, FakeCard{.vramTotal = 1073741824, .hwmon = false});
        GpuReading r = readGpu(findGpus(sys.path()).amd.first());
        QCOMPARE(r.load, 3);
        QVERIFY(!r.edgeTemp && !r.power && !r.fanRpm);

        test::addCard(sys.path(), 1);
        test::setCardFile(sys.path(), 1, QStringLiteral("hwmon/hwmon1/temp1_input"), "warm");
        test::setCardFile(sys.path(), 1, QStringLiteral("gpu_busy_percent"), "-3");
        test::setCardFile(sys.path(), 1, QStringLiteral("hwmon/hwmon1/power1_average"), "");
        test::setCardFile(sys.path(), 1, QStringLiteral("mem_info_vram_used"), "99999999999999999999999");
        r = readGpu(findGpus(sys.path()).amd.first()); // card1 has more memory, so it is first
        QVERIFY(!r.edgeTemp);
        QVERIFY(!r.load);
        QVERIFY(!r.power);
        QVERIFY(!r.vramUsed);
        QCOMPARE(r.hotspotTemp, 46.0);
    }

    void monitorReportsStatus()
    {
        QTemporaryDir empty;
        QCOMPARE(GpuMonitor(empty.path()).status(), int(GpuMonitor::NoCard));

        QTemporaryDir other;
        test::addCard(other.path(), 0, FakeCard{.vendor = QStringLiteral("0x10de")});
        QCOMPARE(GpuMonitor(other.path()).status(), int(GpuMonitor::OtherCards));

        QTemporaryDir sys;
        threeCards(sys.path());
        GpuMonitor monitor(sys.path());
        QCOMPARE(monitor.status(), int(GpuMonitor::AmdCard));
        QCOMPARE(monitor.cardNames().size(), 2);
    }

    void monitorReadsOnlyWhileActive()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor monitor(sys.path(), nullptr, 40);
        QSignalSpy readings(&monitor, &GpuMonitor::readingChanged);
        QTest::qWait(150);
        QCOMPARE(readings.count(), 0);
        QCOMPARE(monitor.temperature(), QStringLiteral("—"));

        monitor.setActive(true);
        QCOMPARE(readings.count(), 1); // a reading right away, not after a second
        QTRY_VERIFY(readings.count() >= 3);
        monitor.setActive(false);
        const int count = int(readings.count());
        QTest::qWait(150);
        QCOMPARE(readings.count(), count);
    }

    void monitorFormatsReadings()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor monitor(sys.path());
        monitor.sample();
        QCOMPARE(monitor.temperature(), QStringLiteral("41 °C"));
        QCOMPARE(monitor.hotspot(), QStringLiteral("46 °C"));
        QCOMPARE(monitor.memoryTemperature(), QStringLiteral("52 °C"));
        QCOMPARE(monitor.power(), QStringLiteral("21 / 315 W"));
        QCOMPARE(monitor.load(), QStringLiteral("3 %"));
        QCOMPARE(monitor.videoMemory(), QStringLiteral("1.0 / 20.0 GB"));
        QCOMPARE(monitor.coreClock(), QStringLiteral("28 MHz"));
        QCOMPARE(monitor.fan(), QStringLiteral("0 RPM · 0%"));
        QCOMPARE(monitor.voltage(), QStringLiteral("632 mV"));

        QVERIFY(QFile::remove(sys.filePath(QStringLiteral("class/drm/card0/device/hwmon/hwmon0/power1_cap"))));
        QVERIFY(QFile::remove(sys.filePath(QStringLiteral("class/drm/card0/device/hwmon/hwmon0/fan1_input"))));
        monitor.sample();
        QCOMPARE(monitor.power(), QStringLiteral("21 W"));
        QCOMPARE(monitor.fan(), QStringLiteral("0%"));
    }

    void monitorKeepsAMinuteOfHistory()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor monitor(sys.path());
        for (int i = 0; i < 75; ++i) {
            test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/temp1_input"), QByteArray::number(30000 + i * 1000));
            monitor.sample();
        }
        QCOMPARE(monitor.edgeHistory().size(), GpuMonitor::HistoryLength);
        QCOMPARE(monitor.edgeHistory().first(), 45.0); // the 15 oldest dropped
        QCOMPARE(monitor.edgeHistory().last(), 104.0);
        QCOMPARE(monitor.powerHistory().size(), GpuMonitor::HistoryLength);
        // Graph ranges cover every value with a margin, in round steps.
        QCOMPARE(monitor.temperatureLow(), 40.0);
        QCOMPARE(monitor.temperatureHigh(), 110.0);
        QCOMPARE(monitor.powerHigh(), 350.0);
    }

    void monitorStartsAfreshAfterAPause()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor monitor(sys.path(), nullptr, 1000);
        monitor.setGapMs(100);
        monitor.setActive(true);
        monitor.sample();
        QCOMPARE(monitor.edgeHistory().size(), 2);
        monitor.setActive(false);
        QTest::qWait(250);
        monitor.setActive(true);
        QCOMPARE(monitor.edgeHistory().size(), 1);
    }

    void monitorSwitchesCards()
    {
        QTemporaryDir sys;
        threeCards(sys.path());
        test::setCardFile(sys.path(), 0, QStringLiteral("hwmon/hwmon0/temp1_input"), "55000");
        GpuMonitor monitor(sys.path());
        QSignalSpy cards(&monitor, &GpuMonitor::cardsChanged);
        monitor.sample();
        QCOMPARE(monitor.temperature(), QStringLiteral("41 °C"));
        monitor.setCurrentCard(1);
        QCOMPARE(cards.count(), 1);
        QVERIFY(monitor.edgeHistory().isEmpty()); // another card's history
        monitor.sample();
        QCOMPARE(monitor.temperature(), QStringLiteral("55 °C"));
        monitor.setCurrentCard(7);
        monitor.setCurrentCard(-1);
        QCOMPARE(monitor.currentCard(), 1);

        test::addCard(sys.path(), 3, FakeCard{.vramTotal = 34359738368ull});
        monitor.rescan();
        QCOMPARE(monitor.cardNames().size(), 3);
        QCOMPARE(monitor.currentCard(), 0);
    }

    // Files recorded on a real RX 7900 XT, idle and with a game running.
    void readsARealRx7900Xt_data()
    {
        QTest::addColumn<QString>("report");
        QTest::addColumn<int>("load");
        QTest::addColumn<int>("coreClock");
        QTest::addColumn<double>("power");
        QTest::addColumn<double>("hotspot");
        QTest::addColumn<int>("fanRpm");
        QTest::addColumn<int>("voltage");
        QTest::addColumn<QString>("videoMemory");
        QTest::newRow("idle") << QStringLiteral("data/rx7900xt-idle.txt") << 69 << 26 << 84.0 << 68.0 << 592 << 663
                              << QStringLiteral("4.2 / 20.0 GB");
        QTest::newRow("game") << QStringLiteral("data/rx7900xt-game.txt") << 32 << 1260 << 86.0 << 69.0 << 591 << 650
                              << QStringLiteral("4.5 / 20.0 GB");
    }

    void readsARealRx7900Xt()
    {
        QFETCH(QString, report);
        QFETCH(int, load);
        QFETCH(int, coreClock);
        QFETCH(double, power);
        QFETCH(double, hotspot);
        QFETCH(int, fanRpm);
        QFETCH(int, voltage);
        QFETCH(QString, videoMemory);

        QTemporaryDir sys;
        QTemporaryDir data;
        const QString path = QFINDTESTDATA(report);
        QVERIFY2(!path.isEmpty(), "card report not found");
        QVERIFY(test::loadCardReport(path, sys.path()));

        const GpuScan scan = findGpus(sys.path(), {test::writeNameTable(data.path())});
        QCOMPARE(scan.amd.size(), 1);
        QCOMPARE(scan.otherCards, 0);
        const AmdGpu &card = scan.amd.first();
        QCOMPARE(card.name, QStringLiteral("AMD Radeon RX 7900 XT"));
        QCOMPARE(card.vramBytes, quint64(21458059264ull));

        const GpuReading r = readGpu(card);
        QVERIFY(!r.asleep);
        QCOMPARE(r.edgeTemp, 62.0);
        QCOMPARE(r.hotspotTemp, hotspot);
        QCOMPARE(r.memoryTemp, 74.0);
        QCOMPARE(r.power, power);
        QCOMPARE(r.powerLimit, 282.0);
        QCOMPARE(r.load, load);
        QCOMPARE(r.coreClock, coreClock);
        QCOMPARE(r.memoryClock, 772);
        QCOMPARE(r.fanRpm, fanRpm);
        QCOMPARE(r.fanPercent, 15);
        QCOMPARE(r.voltage, voltage);

        GpuMonitor monitor(sys.path());
        monitor.sample();
        QCOMPARE(monitor.power(), QStringLiteral("%1 / 282 W").arg(qRound(power)));
        QCOMPARE(monitor.fan(), QStringLiteral("%1 RPM · 15%").arg(fanRpm));
        QCOMPARE(monitor.videoMemory(), videoMemory);
    }

    // Bits of gpu_metrics' throttle status, grouped into words.
    void namesWhatHoldsTheCardBack_data()
    {
        QTest::addColumn<QByteArray>("metrics");
        QTest::addColumn<double>("hotspot");
        QTest::addColumn<QStringList>("reasons");
        const QStringList none;
        QTest::newRow("nothing") << metricsWith(0) << 60.0 << none;
        QTest::newRow("package power") << metricsWith(bit(0)) << 60.0 << QStringList{QStringLiteral("power limit")};
        QTest::newRow("fast power") << metricsWith(bit(5)) << 60.0 << QStringList{QStringLiteral("power limit")};
        QTest::newRow("not a reason") << metricsWith(bit(8) | bit(24) | bit(48) | bit(63)) << 60.0 << none;
        QTest::newRow("graphics current") << metricsWith(bit(16)) << 60.0 << QStringList{QStringLiteral("current limit")};
        QTest::newRow("edge temperature") << metricsWith(bit(35)) << 60.0 << QStringList{QStringLiteral("GPU temperature")};
        QTest::newRow("memory temperature") << metricsWith(bit(34)) << 60.0 << QStringList{QStringLiteral("memory temperature")};
        QTest::newRow("voltage regulator") << metricsWith(bit(38)) << 60.0 << QStringList{QStringLiteral("board temperature")};
        QTest::newRow("reliability") << metricsWith(bit(57)) << 60.0 << QStringList{QStringLiteral("reliability limit")};
        // RDNA 3 firmware sets the hotspot bit long before the card slows
        // down; it counts only from 90 °C, as MangoHud does.
        QTest::newRow("hotspot, cool") << metricsWith(bit(36)) << 89.9 << none;
        QTest::newRow("hotspot, hot") << metricsWith(bit(36)) << 90.0 << QStringList{QStringLiteral("hotspot temperature")};
        QTest::newRow("hotspot, unknown") << metricsWith(bit(36)) << -1.0 << none;
        QTest::newRow("several, in order") << metricsWith(bit(57) | bit(36) | bit(16) | bit(1)) << 95.0
                                           << QStringList{QStringLiteral("power limit"), QStringLiteral("current limit"),
                                                          QStringLiteral("hotspot temperature"), QStringLiteral("reliability limit")};
    }

    void namesWhatHoldsTheCardBack()
    {
        QFETCH(QByteArray, metrics);
        QFETCH(double, hotspot);
        QFETCH(QStringList, reasons);
        const auto read = throttleReasons(metrics, hotspot < 0 ? std::nullopt : std::optional<double>(hotspot));
        QVERIFY(read);
        QCOMPARE(*read, reasons);
    }

    void ignoresOtherMetricsLayouts()
    {
        QVERIFY(!throttleReasons({}, 50.0));
        QVERIFY(!throttleReasons(metricsWith(bit(0)).left(119), 50.0));
        QVERIFY(!throttleReasons(metricsWith(bit(0), 2), 50.0)); // version 1.2 has no such field
        QByteArray version2 = metricsWith(bit(0));
        version2[2] = 2;
        QVERIFY(!throttleReasons(version2, 50.0));
    }

    // The card reports recorded on a real 7900 XT: the hotspot bit is set
    // during the game at 69 °C, which does not count.
    void readsWhatHoldsARealCardBack()
    {
        for (const char *report : {"data/rx7900xt-idle.txt", "data/rx7900xt-game.txt"}) {
            QTemporaryDir sys;
            const QString path = QFINDTESTDATA(report);
            QVERIFY(test::loadCardReport(path, sys.path()));
            const GpuScan scan = findGpus(sys.path());
            QCOMPARE(scan.amd.size(), 1);
            const GpuReading r = readGpu(scan.amd.first());
            QVERIFY2(r.heldBackBy, report);
            QCOMPARE(*r.heldBackBy, QStringList());

            GpuMonitor monitor(sys.path());
            monitor.sample();
            QCOMPARE(monitor.heldBackBy(), QStringLiteral("Nothing"));
        }
    }

    void monitorFormatsWhatHoldsTheCardBack()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor monitor(sys.path());
        monitor.sample();
        QCOMPARE(monitor.heldBackBy(), QStringLiteral("—")); // no gpu_metrics file

        QVERIFY(test::writeFile(sys.filePath(QStringLiteral("class/drm/card0/device/gpu_metrics")), metricsWith(bit(0) | bit(34))));
        monitor.sample();
        QCOMPARE(monitor.heldBackBy(), QStringLiteral("Power limit, memory temperature"));
    }

    void addsUpAGamesVideoMemory()
    {
        QTemporaryDir proc;
        // The game and its helper share one connection to the card; it counts once.
        fakeFdinfo(proc.path(), 10, 5, amdgpuClient(7, "1048576 KiB"));
        fakeFdinfo(proc.path(), 11, 9, amdgpuClient(7, "1048576 KiB"));
        fakeFdinfo(proc.path(), 11, 10, amdgpuClient(8, "512 MiB"));
        // Older kernels name the field drm-memory-vram.
        fakeFdinfo(proc.path(), 12, 3, amdgpuClient(9, "1 GiB", "drm-memory-vram"));
        // No unit: bytes.
        fakeFdinfo(proc.path(), 12, 4, amdgpuClient(10, "4096"));
        // Not the card's: another driver, an ordinary file, damaged values.
        fakeFdinfo(proc.path(), 12, 5, "drm-driver:\ti915\ndrm-client-id:\t11\ndrm-resident-vram:\t9 GiB\n");
        fakeFdinfo(proc.path(), 12, 6, "pos:\t0\nflags:\t0100000\n");
        fakeFdinfo(proc.path(), 12, 7, amdgpuClient(12, "lots KiB"));
        fakeFdinfo(proc.path(), 12, 8, "drm-driver:\tamdgpu\ndrm-resident-vram:\t1 GiB\n"); // no client id

        constexpr quint64 MiB = 1024ull * 1024;
        QCOMPARE(gpu::processVideoMemory({10, 11, 12}, proc.path()), std::optional<quint64>(1024 * MiB + 512 * MiB + 1024 * MiB + 4096));
        QCOMPARE(gpu::processVideoMemory({10}, proc.path()), std::optional<quint64>(1024 * MiB));
        // A process that ended, or has nothing on the card.
        QVERIFY(!gpu::processVideoMemory({99}, proc.path()));
        QVERIFY(!gpu::processVideoMemory({}, proc.path()));
        QVERIFY(!gpu::processVideoMemory({10}, QStringLiteral("/does/not/exist")));
    }

    void readsTheProcessorLoad()
    {
        QTemporaryDir proc;
        const QString stat = proc.filePath(QStringLiteral("stat"));
        test::writeFile(stat, "cpu  1000 50 450 8000 500 0 0 0 100 0\ncpu0 500 25 225 4000 250 0 0 0 50 0\nintr 12345\n");
        const auto before = cpu::readCpuTimes(proc.path());
        QVERIFY(before);
        QCOMPARE(before->total, quint64(10000)); // guest time is inside user time
        QCOMPARE(before->busy, quint64(1500));
        // A quarter busy since.
        test::writeFile(stat, "cpu  1200 50 500 8700 550 0 0 0 100 0\n");
        const auto after = cpu::readCpuTimes(proc.path());
        QVERIFY(after);
        QCOMPARE(cpu::cpuLoad(*before, *after), std::optional<int>(25));
        // No time between, counters that went back.
        QVERIFY(!cpu::cpuLoad(*after, *after));
        QVERIFY(!cpu::cpuLoad(*after, *before));

        for (const QByteArray &damaged :
             {QByteArray(), QByteArray("intr 1 2 3\n"), QByteArray("cpu  1 2\n"), QByteArray("cpu  1 2 x 4 5\n")}) {
            test::writeFile(stat, damaged);
            QVERIFY2(!cpu::readCpuTimes(proc.path()), damaged.constData());
        }
        QVERIFY(!cpu::readCpuTimes(QStringLiteral("/does/not/exist")));
        // Old kernels list only four counters.
        test::writeFile(stat, "cpu  100 0 100 800\n");
        QCOMPARE(cpu::readCpuTimes(proc.path())->busy, quint64(200));
    }

    void readsTheProcessorTemperature()
    {
        QTemporaryDir amd;
        fakeSensor(amd.path(), 0, "amdgpu", {{"edge", 50000}});
        fakeSensor(amd.path(), 1, "nvme", {{"Composite", 40000}});
        fakeSensor(amd.path(), 2, "k10temp", {{"Tccd1", 61000}, {"Tctl", 67250}, {"Tccd2", 58000}});
        QCOMPARE(cpu::cpuTemperature(amd.path()), std::optional<double>(67.25));

        QTemporaryDir intel;
        fakeSensor(intel.path(), 0, "coretemp", {{"Core 0", 55000}, {"Core 1", 57000}, {"Package id 0", 60000}});
        QCOMPARE(cpu::cpuTemperature(intel.path()), std::optional<double>(60.0));

        QTemporaryDir zen;
        fakeSensor(zen.path(), 0, "zenpower", {{"Tdie", 48000}, {"Tctl", 58000}});
        QCOMPARE(cpu::cpuTemperature(zen.path()), std::optional<double>(48.0));

        // No labels: the first input.
        QTemporaryDir unlabelled;
        fakeSensor(unlabelled.path(), 0, "k10temp", {{"", 44000}, {"", 70000}});
        QCOMPARE(cpu::cpuTemperature(unlabelled.path()), std::optional<double>(44.0));

        QTemporaryDir none;
        fakeSensor(none.path(), 0, "amdgpu", {{"edge", 50000}});
        fakeSensor(none.path(), 1, "k10temp", {});
        QVERIFY(!cpu::cpuTemperature(none.path()));
        QVERIFY(!cpu::cpuTemperature(QStringLiteral("/does/not/exist")));
    }

    void readsTheProcessorClock()
    {
        QTemporaryDir sys;
        const QString cpus = sys.filePath(QStringLiteral("devices/system/cpu/"));
        test::writeFile(cpus + QStringLiteral("cpu0/cpufreq/scaling_cur_freq"), "3000000\n");
        test::writeFile(cpus + QStringLiteral("cpu1/cpufreq/scaling_cur_freq"), "4500000\n");
        test::writeFile(cpus + QStringLiteral("cpu2/cpufreq/scaling_cur_freq"), "0\n");     // offline
        test::writeFile(cpus + QStringLiteral("cpu3/cpufreq/scaling_cur_freq"), "fast\n");  // damaged
        QDir().mkpath(cpus + QStringLiteral("cpu4"));                                         // no cpufreq
        test::writeFile(cpus + QStringLiteral("cpufreq/policy0/scaling_cur_freq"), "9000000\n");
        test::writeFile(cpus + QStringLiteral("cpuidle/scaling_cur_freq"), "9000000\n");
        QCOMPARE(cpu::cpuClock(sys.path()), std::optional<int>(3750));

        QTemporaryDir empty;
        QVERIFY(!cpu::cpuClock(empty.path()));
    }

    // The processor is read with any card, also with none.
    void monitorShowsTheProcessor()
    {
        QTemporaryDir sys;
        QTemporaryDir proc;
        fakeSensor(sys.path(), 0, "k10temp", {{"Tctl", 63400}});
        test::writeFile(sys.filePath(QStringLiteral("devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")), "4250000\n");
        test::writeFile(proc.filePath(QStringLiteral("stat")), "cpu  100 0 100 800 0 0 0 0 0 0\n");

        GpuMonitor monitor(sys.path());
        monitor.setGameSources({}, proc.path());
        QCOMPARE(monitor.status(), int(GpuMonitor::NoCard));
        monitor.sample();
        QCOMPARE(monitor.cpuLoad(), QStringLiteral("—")); // needs two readings
        QCOMPARE(monitor.cpuTemperature(), QStringLiteral("63 °C"));
        QCOMPARE(monitor.cpuClock(), QStringLiteral("4.25 GHz"));
        test::writeFile(proc.filePath(QStringLiteral("stat")), "cpu  160 0 140 900 0 0 0 0 0 0\n");
        monitor.sample();
        QCOMPARE(monitor.cpuLoad(), QStringLiteral("50 %"));
    }

    void monitorShowsTheGameBeingMeasured()
    {
        QTemporaryDir sys;
        QTemporaryDir proc;
        QTemporaryDir perf;
        test::addCard(sys.path(), 0);
        const auto live = [&perf](quint32 appId, qint64 time, double fps) {
            test::writeFile(perf.filePath(QStringLiteral("%1/live").arg(appId)),
                            QStringLiteral("time=%1\nfps=%2\nframe_ms=%3\nframe_ms_max=%4\n")
                                .arg(time)
                                .arg(fps)
                                .arg(1000.0 / fps)
                                .arg(2000.0 / fps)
                                .toUtf8());
        };
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        live(1001, now - 60, 30); // a game that stopped a minute ago
        live(1002, now, 144);
        QDir().mkpath(perf.filePath(QStringLiteral("not-a-game")));
        test::writeFile(perf.filePath(QStringLiteral("not-a-game/live")), QStringLiteral("time=%1\nfps=1\n").arg(now).toUtf8());

        // The game's processes: Steam's reaper, the game below it, and what holds memory on the card.
        const QByteArray reaper = "/x/Steam/ubuntu12_32/reaper";
        test::writeFile(proc.filePath(QStringLiteral("50/comm")), "reaper\n");
        test::writeFile(proc.filePath(QStringLiteral("50/cmdline")),
                        reaper + '\0' + "SteamLaunch" + '\0' + "AppId=1002" + '\0' + "--" + '\0' + "./game" + '\0');
        test::writeFile(proc.filePath(QStringLiteral("50/stat")), "50 (reaper) S 1 50 50 0\n");
        test::writeFile(proc.filePath(QStringLiteral("51/stat")), "51 (game) S 50 50 50 0\n");
        fakeFdinfo(proc.path(), 51, 4, amdgpuClient(3, "3145728 KiB"));

        GpuMonitor monitor(sys.path());
        monitor.setGameSources(perf.path(), proc.path());
        monitor.sample();
        QCOMPARE(monitor.gameAppId(), 1002);
        QCOMPARE(monitor.gameFps(), QStringLiteral("144 FPS"));
        QCOMPARE(monitor.gameFrameTime(), QStringLiteral("6.9 ms · slowest 13.9 ms"));
        QCOMPARE(monitor.gameVideoMemory(), QStringLiteral("3.0 GB"));
        QCOMPARE(monitor.fpsHistory(), QList<qreal>{144});
        QCOMPARE(monitor.fpsHigh(), 150.0); // in steps of 30

        live(1002, now, 90);
        monitor.sample();
        QCOMPARE(monitor.fpsHistory(), (QList<qreal>{144, 90}));

        // The game stops: its block goes, and a new game starts its own graph.
        live(1002, now - 10, 90);
        monitor.sample();
        QCOMPARE(monitor.gameAppId(), 0);
        QCOMPARE(monitor.gameFps(), QStringLiteral("—"));
        QCOMPARE(monitor.gameVideoMemory(), QStringLiteral("—"));
        live(1001, now, 40);
        monitor.sample();
        QCOMPARE(monitor.gameAppId(), 1001);
        QCOMPARE(monitor.fpsHistory(), QList<qreal>{40});
        QCOMPARE(monitor.fpsHigh(), 60.0);
        QCOMPARE(monitor.gameVideoMemory(), QStringLiteral("—")); // no reaper for it
    }

    void showsNothingWhileTheCardSleeps()
    {
        QTemporaryDir sys;
        test::addCard(sys.path(), 0, FakeCard{.runtimeStatus = QStringLiteral("suspended")});
        GpuMonitor monitor(sys.path());
        monitor.sample();
        QVERIFY(monitor.asleep());
        QVERIFY(monitor.edgeHistory().isEmpty());
        QCOMPARE(monitor.power(), QStringLiteral("—"));
    }
};

QTEST_GUILESS_MAIN(GpuTest)
#include "tst_gpu.moc"
