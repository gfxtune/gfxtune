// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "support/Fixtures.h"
#include "vdf/BinaryVdf.h"

#include <QTest>
#include <QtEndian>

#include <bit>

using namespace gfxtune;
using vdf::Node;

namespace
{

template<typename T>
QByteArray le(T v)
{
    QByteArray out(int(sizeof(T)), '\0');
    qToLittleEndian(v, out.data());
    return out;
}

QByteArray cstr(const char *text)
{
    return QByteArray(text) + '\0';
}

} // namespace

class BinaryVdfTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsEveryValueType()
    {
        QByteArray data;
        data += char(0x00) + cstr("s");
        data += char(0x01) + cstr("text") + cstr("hello");
        data += char(0x02) + cstr("int") + le<qint32>(-5);
        data += char(0x03) + cstr("float") + le<quint32>(std::bit_cast<quint32>(1.5f));
        data += char(0x04) + cstr("pointer") + le<qint32>(7);
        data += char(0x05) + cstr("wide") + QByteArray("h\0\xE9\0\0\0", 6);
        data += char(0x06) + cstr("color") + le<qint32>(0x00FF00);
        data += char(0x07) + cstr("u64") + le<quint64>(18446744073709551615ull);
        data += char(0x0A) + cstr("i64") + le<qint64>(-9000000000ll);
        data += char(0x08); // end of "s"
        data += char(0x08); // end of top level

        Node root;
        QString error;
        QVERIFY2(vdf::parseBinary(data, root, &error), qPrintable(error));
        const Node *s = root.child(u"s");
        QVERIFY(s && s->isSection);
        QCOMPARE(s->valueOf(u"text"), QStringLiteral("hello"));
        QCOMPARE(s->valueOf(u"int"), QStringLiteral("-5"));
        QCOMPARE(s->valueOf(u"float"), QStringLiteral("1.5"));
        QCOMPARE(s->valueOf(u"pointer"), QStringLiteral("7"));
        QCOMPARE(s->valueOf(u"wide"), QStringLiteral("hé"));
        QCOMPARE(s->valueOf(u"color"), QStringLiteral("65280"));
        QCOMPARE(s->valueOf(u"u64"), QStringLiteral("18446744073709551615"));
        QCOMPARE(s->valueOf(u"i64"), QStringLiteral("-9000000000"));
    }

    void readsKeysFromKeyTable()
    {
        const QStringList table{QStringLiteral("appinfo"), QStringLiteral("appid"), QStringLiteral("common")};
        QByteArray data;
        data += char(0x00) + le<qint32>(0);
        data += char(0x01) + le<qint32>(1) + cstr("1001");
        data += char(0x00) + le<qint32>(2) + char(0x08);
        data += char(0x08);
        data += char(0x08);
        Node root;
        QString error;
        QVERIFY2(vdf::parseBinary(data, root, &error, &table), qPrintable(error));
        QCOMPARE(root.find({u"appinfo", u"appid"})->value, QStringLiteral("1001"));
        QVERIFY(root.find({u"appinfo", u"common"})->isSection);
    }

    void acceptsAlternativeEndMarker()
    {
        QByteArray data = char(0x00) + cstr("s") + char(0x01) + cstr("k") + cstr("v") + char(0x0B) + char(0x0B);
        Node root;
        QVERIFY(vdf::parseBinary(data, root));
        QCOMPARE(root.find({u"s", u"k"})->value, QStringLiteral("v"));
    }

    void ignoresBytesAfterTheEnd()
    {
        QByteArray data = char(0x01) + cstr("k") + cstr("v") + char(0x08) + QByteArray("trailing junk");
        Node root;
        QVERIFY(vdf::parseBinary(data, root));
        QCOMPARE(root.valueOf(u"k"), QStringLiteral("v"));
    }

    void rejectsDamagedData_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<bool>("withTable");
        QTest::newRow("unknown type") << QByteArray(char(0x09) + cstr("k")) << false;
        QTest::newRow("key cut off") << QByteArray(
            "\x01"
            "abc",
            4) << false;
        QTest::newRow("string cut off") << QByteArray(char(0x01) + cstr("k") + "abc") << false;
        QTest::newRow("int cut off") << QByteArray(char(0x02) + cstr("k") + "ab") << false;
        QTest::newRow("u64 cut off") << QByteArray(char(0x07) + cstr("k") + "abcd") << false;
        QTest::newRow("wide cut off") << QByteArray(char(0x05) + cstr("k") + QByteArray("h\0i", 3)) << false;
        QTest::newRow("section never closed") << QByteArray(char(0x00) + cstr("s") + char(0x01) + cstr("k") + cstr("v")) << false;
        QTest::newRow("key index too big") << QByteArray(char(0x01) + le<qint32>(5) + cstr("v")) << true;
        QTest::newRow("negative key index") << QByteArray(char(0x01) + le<qint32>(-1) + cstr("v")) << true;
        QTest::newRow("key index cut off") << QByteArray("\x01\x00", 2) << true;
    }

    void rejectsDamagedData()
    {
        QFETCH(QByteArray, data);
        QFETCH(bool, withTable);
        const QStringList table{QStringLiteral("only")};
        Node root;
        QString error;
        QVERIFY(!vdf::parseBinary(data, root, &error, withTable ? &table : nullptr));
        QVERIFY(!error.isEmpty());
    }

    void limitsNesting()
    {
        QByteArray data;
        for (int i = 0; i < 5000; ++i) {
            data += char(0x00) + cstr("s");
        }
        Node root;
        QString error;
        QVERIFY(!vdf::parseBinary(data, root, &error));
        QVERIFY(error.contains(QLatin1String("deeply")));
    }

    void roundTripsWrittenData()
    {
        Node original;
        original.isSection = true;
        original.children.push_back(test::section(
            QStringLiteral("appinfo"),
            {
                test::section(QStringLiteral("common"), {test::value(QStringLiteral("name"), QStringLiteral("Ünïcode Game"))}),
            }));
        for (bool withTable : {false, true}) {
            QStringList table;
            const QByteArray data = test::writeBinary(original, withTable ? &table : nullptr);
            Node parsed;
            QString error;
            QVERIFY2(vdf::parseBinary(data, parsed, &error, withTable ? &table : nullptr), qPrintable(error));
            QCOMPARE(parsed.find({u"appinfo", u"common", u"name"})->value, QStringLiteral("Ünïcode Game"));
        }
    }

    void survivesDamagedInput()
    {
        Node original;
        original.isSection = true;
        original.children.push_back(test::section(
            QStringLiteral("appinfo"),
            {
                test::value(QStringLiteral("appid"), QStringLiteral("1001")),
                test::section(QStringLiteral("config"), {test::value(QStringLiteral("executable"), QStringLiteral("game.exe"))}),
            }));
        QStringList table;
        const QByteArray plain = test::writeBinary(original);
        const QByteArray indexed = test::writeBinary(original, &table);
        std::mt19937 random(7);
        for (int i = 0; i < 20000; ++i) {
            Node root;
            vdf::parseBinary(test::mutate(plain, random), root);
            vdf::parseBinary(test::mutate(indexed, random), root, nullptr, &table);
        }
    }
};

QTEST_GUILESS_MAIN(BinaryVdfTest)
#include "tst_binaryvdf.moc"
