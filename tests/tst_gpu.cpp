// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GpuMonitor.h"
#include "gpu/AmdGpu.h"
#include "support/FakeSysfs.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using namespace gfxtune::gpu;
using test::FakeCard;

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
        QTest::newRow("idle") << QStringLiteral("data/rx7900xt-idle.txt") << 69 << 26 << 84.0 << 68.0 << 592 << 663 << QStringLiteral("4.2 / 20.0 GB");
        QTest::newRow("game") << QStringLiteral("data/rx7900xt-game.txt") << 32 << 1260 << 86.0 << 69.0 << 591 << 650 << QStringLiteral("4.5 / 20.0 GB");
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
