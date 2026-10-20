// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "steam/AppInfo.h"
#include "support/Fixtures.h"

#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace gfxtune;
using test::section;
using test::value;

namespace
{

QList<QPair<quint32, vdf::Node>> sampleApps()
{
    return {
        {10, section({}, {value(QStringLiteral("appid"), QStringLiteral("10")),
                          section(QStringLiteral("common"), {value(QStringLiteral("name"), QStringLiteral("First")),
                                                             value(QStringLiteral("type"), QStringLiteral("Game"))})})},
        {20, section({}, {value(QStringLiteral("appid"), QStringLiteral("20")),
                          section(QStringLiteral("common"), {value(QStringLiteral("name"), QStringLiteral("Second"))})})},
        {30, section({}, {value(QStringLiteral("appid"), QStringLiteral("30")),
                          section(QStringLiteral("common"), {value(QStringLiteral("name"), QStringLiteral("Third"))})})},
    };
}

} // namespace

class AppInfoTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString store(const QByteArray &data)
    {
        static int counter = 0;
        const QString path = m_dir.filePath(QStringLiteral("appinfo-%1.vdf").arg(++counter));
        test::writeFile(path, data);
        return path;
    }

private Q_SLOTS:
    void readsRequestedApps_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("version 28") << 28;
        QTest::newRow("version 29") << 29;
    }

    void readsRequestedApps()
    {
        QFETCH(int, version);
        const QString path = store(test::writeAppInfo(version, sampleApps()));
        steam::AppInfoFile file;
        QString error;
        QVERIFY2(steam::readAppInfo(path, {10, 30, 99}, file, &error), qPrintable(error));
        QCOMPARE(file.version, version);
        QCOMPARE(file.apps.size(), 2);
        QCOMPARE(file.apps.value(10).find({u"common", u"name"})->value, QStringLiteral("First"));
        QCOMPARE(file.apps.value(30).find({u"common", u"name"})->value, QStringLiteral("Third"));
        QVERIFY(!file.apps.contains(20));
        QVERIFY(file.warnings.isEmpty());
    }

    void handlesEmptyRequestAndEmptyFile()
    {
        steam::AppInfoFile file;
        QVERIFY(steam::readAppInfo(store(test::writeAppInfo(29, sampleApps())), {}, file));
        QVERIFY(file.apps.isEmpty());
        QVERIFY(steam::readAppInfo(store(test::writeAppInfo(29, {})), {10}, file));
        QVERIFY(file.apps.isEmpty());
    }

    void refusesUnknownVersions()
    {
        QByteArray data = test::writeAppInfo(29, sampleApps());
        qToLittleEndian<quint32>(0x0756442A, data.data()); // a future version 30
        steam::AppInfoFile file;
        QString error;
        QVERIFY(!steam::readAppInfo(store(data), {10}, file, &error));
        QVERIFY(error.contains(QLatin1String("unsupported")));
    }

    void rejectsDamagedFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        const QByteArray v29 = test::writeAppInfo(29, sampleApps());
        const QByteArray v28 = test::writeAppInfo(28, sampleApps());

        QTest::newRow("too short") << QByteArray("\x29\x44\x56", 3);
        QTest::newRow("v29 header only") << v29.left(12);

        QByteArray badTable = v29;
        qToLittleEndian<qint64>(qint64(v29.size()) + 100, badTable.data() + 8);
        QTest::newRow("key table outside file") << badTable;

        QByteArray negativeTable = v29;
        qToLittleEndian<qint64>(-1, negativeTable.data() + 8);
        QTest::newRow("negative key table offset") << negativeTable;

        QByteArray hugeCount = v29;
        const qint64 tableAt = qFromLittleEndian<qint64>(v29.constData() + 8);
        qToLittleEndian<qint32>(2000000000, hugeCount.data() + tableAt);
        QTest::newRow("impossible key count") << hugeCount;

        QByteArray badSize = v28;
        qToLittleEndian<quint32>(0x7FFFFFFF, badSize.data() + 8 + 4);
        QTest::newRow("entry size past the end") << badSize;

        QByteArray tinySize = v28;
        qToLittleEndian<quint32>(4, tinySize.data() + 8 + 4);
        QTest::newRow("entry size too small") << tinySize;

        QTest::newRow("entry cut off") << v28.left(8 + 30);
    }

    void rejectsDamagedFiles()
    {
        QFETCH(QByteArray, data);
        steam::AppInfoFile file;
        QString error;
        QVERIFY(!steam::readAppInfo(store(data), {10, 20, 30}, file, &error));
        QVERIFY(!error.isEmpty());
    }

    void keepsGoingPastOneBrokenEntry()
    {
        QByteArray data = test::writeAppInfo(28, sampleApps());
        // Damage the first entry's data (an unknown value type) but keep its
        // size, so the reader can step over it.
        data[8 + 68] = char(0x09);
        steam::AppInfoFile file;
        QString error;
        QVERIFY2(steam::readAppInfo(store(data), {10, 20}, file, &error), qPrintable(error));
        QVERIFY(!file.apps.contains(10));
        QCOMPARE(file.apps.value(20).find({u"common", u"name"})->value, QStringLiteral("Second"));
        QCOMPARE(file.warnings.size(), 1);
    }

    void reportsMissingFile()
    {
        steam::AppInfoFile file;
        QString error;
        QVERIFY(!steam::readAppInfo(m_dir.filePath(QStringLiteral("missing.vdf")), {10}, file, &error));
        QVERIFY(!error.isEmpty());
    }

    void survivesDamagedInput()
    {
        const QByteArray v28 = test::writeAppInfo(28, sampleApps());
        const QByteArray v29 = test::writeAppInfo(29, sampleApps());
        std::mt19937 random(29);
        const QString path = m_dir.filePath(QStringLiteral("fuzz.vdf"));
        for (int i = 0; i < 3000; ++i) {
            test::writeFile(path, test::mutate(i % 2 ? v28 : v29, random));
            steam::AppInfoFile file;
            steam::readAppInfo(path, {10, 20, 30}, file);
        }
    }
};

QTEST_GUILESS_MAIN(AppInfoTest)
#include "tst_appinfo.moc"
