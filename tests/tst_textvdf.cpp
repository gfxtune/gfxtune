// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "support/Fixtures.h"
#include "vdf/TextVdf.h"

#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using vdf::Node;

class TextVdfTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsNestedSections()
    {
        const QByteArray text =
            "\"libraryfolders\"\n"
            "{\n"
            "\t\"0\"\n"
            "\t{\n"
            "\t\t\"path\"\t\t\"/games/steam\"\n"
            "\t\t\"apps\"\n"
            "\t\t{\n"
            "\t\t\t\"1001\"\t\t\"12345\"\n"
            "\t\t}\n"
            "\t}\n"
            "\t\"contentstatsid\"\t\t\"-123\"\n"
            "}";
        Node root;
        QString error;
        QVERIFY2(vdf::parseText(text, root, &error), qPrintable(error));
        QCOMPARE(root.children.size(), 1u);
        const Node *folder = root.find({u"libraryfolders", u"0"});
        QVERIFY(folder);
        QVERIFY(folder->isSection);
        QCOMPARE(folder->valueOf(u"path"), QStringLiteral("/games/steam"));
        QCOMPARE(root.find({u"libraryfolders", u"0", u"apps", u"1001"})->value, QStringLiteral("12345"));
        QCOMPARE(root.find({u"libraryfolders", u"contentstatsid"})->value, QStringLiteral("-123"));
    }

    void lookupIgnoresCase()
    {
        Node root;
        QVERIFY(vdf::parseText("\"AppState\" { \"StateFlags\" \"4\" }", root));
        QCOMPARE(root.find({u"appstate", u"stateflags"})->value, QStringLiteral("4"));
        QCOMPARE(root.find({u"APPSTATE"})->valueOf(u"STATEFLAGS"), QStringLiteral("4"));
        QVERIFY(!root.find({u"appstate", u"missing"}));
        QCOMPARE(root.find({u"appstate"})->valueOf(u"missing"), QString());
    }

    void valueOfIgnoresSections()
    {
        Node root;
        QVERIFY(vdf::parseText("\"a\" { \"b\" { \"c\" \"1\" } }", root));
        QCOMPARE(root.child(u"a")->valueOf(u"b"), QString());
    }

    void decodesEscapes()
    {
        Node root;
        QVERIFY(vdf::parseText("\"LaunchOptions\" \"A=\\\"x y\\\" \\\\path\\\\ tab\\there\\nnext \\q\"", root));
        QCOMPARE(root.valueOf(u"LaunchOptions"), QStringLiteral("A=\"x y\" \\path\\ tab\there\nnext \\q"));
    }

    void skipsCommentsAndConditionals()
    {
        const QByteArray text =
            "// leading comment\n"
            "\"root\" // after a key\n"
            "{\n"
            "\t\"a\"\t\"1\"\t[$WIN32]\n"
            "\t\"b\" [$LINUX] { \"c\" \"2\" }\n"
            "\t// \"hidden\" \"3\"\n"
            "}";
        Node root;
        QString error;
        QVERIFY2(vdf::parseText(text, root, &error), qPrintable(error));
        const Node *section = root.child(u"root");
        QCOMPARE(section->children.size(), 2u);
        QCOMPARE(section->valueOf(u"a"), QStringLiteral("1"));
        QCOMPARE(section->find({u"b", u"c"})->value, QStringLiteral("2"));
    }

    void acceptsUnquotedTokens()
    {
        Node root;
        QVERIFY(vdf::parseText("key value\nsection { inner 42 }", root));
        QCOMPARE(root.valueOf(u"key"), QStringLiteral("value"));
        QCOMPARE(root.find({u"section", u"inner"})->value, QStringLiteral("42"));
    }

    void handlesBomAndWindowsLineEnds()
    {
        Node root;
        QVERIFY(vdf::parseText("\xEF\xBB\xBF\"a\"\r\n{\r\n\t\"b\"\t\"c\"\r\n}\r\n", root));
        QCOMPARE(root.find({u"a", u"b"})->value, QStringLiteral("c"));
    }

    void keepsDuplicateKeysInOrder()
    {
        Node root;
        QVERIFY(vdf::parseText("\"x\" \"1\" \"x\" \"2\" \"X\" \"3\"", root));
        QCOMPARE(root.children.size(), 3u);
        QCOMPARE(root.valueOf(u"x"), QStringLiteral("1"));
        QCOMPARE(root.children[2].value, QStringLiteral("3"));
    }

    void readsMultiLineAndUnicodeText()
    {
        Node root;
        QVERIFY(vdf::parseText("\"name\" \"Café ☕ ゲーム\nsecond line\"", root));
        QCOMPARE(root.valueOf(u"name"), QStringLiteral("Café ☕ ゲーム\nsecond line"));
    }

    void acceptsEmptyInput()
    {
        Node root;
        QVERIFY(vdf::parseText("", root));
        QVERIFY(root.children.empty());
        QVERIFY(vdf::parseText("  \n// only a comment\n", root));
        QVERIFY(root.children.empty());
    }

    void rejectsDamagedText_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("unterminated quote") << QByteArray("\"a\" \"b");
        QTest::newRow("missing close") << QByteArray("\"a\" { \"b\" \"c\"");
        QTest::newRow("extra close") << QByteArray("\"a\" \"b\" }");
        QTest::newRow("key at end") << QByteArray("\"a\" \"b\" \"c\"");
        QTest::newRow("key before close") << QByteArray("\"s\" { \"a\" }");
        QTest::newRow("open without key") << QByteArray("{ \"a\" \"b\" }");
        QTest::newRow("unclosed conditional") << QByteArray("\"a\" \"b\" [$WIN32\n");
    }

    void rejectsDamagedText()
    {
        QFETCH(QByteArray, text);
        Node root;
        QString error;
        QVERIFY(!vdf::parseText(text, root, &error));
        QVERIFY(!error.isEmpty());
    }

    void limitsNesting()
    {
        auto nested = [](int depth) {
            QByteArray text;
            for (int i = 0; i < depth; ++i) {
                text += "\"s\" { ";
            }
            text += "\"v\" \"1\" ";
            for (int i = 0; i < depth; ++i) {
                text += "} ";
            }
            return text;
        };
        Node root;
        QVERIFY(vdf::parseText(nested(100), root));
        QString error;
        QVERIFY(!vdf::parseText(nested(5000), root, &error));
        QVERIFY(error.contains(QLatin1String("deeply")));
    }

    void readsFilesAndRefusesHugeOnes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("big.vdf"));
        QVERIFY(test::writeFile(path, "\"a\" \"" + QByteArray(4096, 'x') + "\""));
        Node root;
        QVERIFY(vdf::readTextFile(path, root));
        QString error;
        QVERIFY(!vdf::readTextFile(path, root, &error, 1024));
        QVERIFY(!vdf::readTextFile(dir.filePath(QStringLiteral("missing.vdf")), root, &error));
    }

    void roundTripsWrittenText()
    {
        Node original;
        original.isSection = true;
        original.children.push_back(test::section(QStringLiteral("A"),
                                                  {
                                                      test::value(QStringLiteral("quote\"key"), QStringLiteral("back\\slash \"q\"")),
                                                      test::section(QStringLiteral("empty"), {}),
                                                  }));
        Node parsed;
        QVERIFY(vdf::parseText(test::writeText(original), parsed));
        QCOMPARE(parsed.find({u"A", u"quote\"key"})->value, QStringLiteral("back\\slash \"q\""));
        QVERIFY(parsed.find({u"A", u"empty"})->isSection);
    }

    void survivesDamagedInput()
    {
        const QByteArray sample =
            "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { \"1001\" { \"LaunchOptions\" \"x \\\"y\\\" "
            "%command%\" } } } } } }";
        std::mt19937 random(20261020);
        for (int i = 0; i < 20000; ++i) {
            Node root;
            vdf::parseText(test::mutate(sample, random), root);
        }
        for (int i = 0; i < 2000; ++i) {
            QByteArray noise(int(random() % 512), '\0');
            for (char &c : noise) {
                c = char(random() % 256);
            }
            Node root;
            vdf::parseText(noise, root);
        }
    }
};

QTEST_GUILESS_MAIN(TextVdfTest)
#include "tst_textvdf.moc"
