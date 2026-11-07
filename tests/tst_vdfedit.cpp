// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "support/Fixtures.h"
#include "vdf/TextVdf.h"
#include "vdf/TextVdfEdit.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace gfxtune;
using vdf::Node;
using vdf::ValueEdit;

namespace
{

const QStringList AppPath{QStringLiteral("Store"), QStringLiteral("apps"), QStringLiteral("10")};

QByteArray sample()
{
    return "// Steam keeps comments rarely, but the format allows them\n"
           "\"Store\"\n{\n\t\"apps\"\n\t{\n\t\t\"10\"\n\t\t{\n\t\t\t\"LastPlayed\"\t\t\"1\"\n\t\t\t\"LaunchOptions\"\t\t\"old\"\n\t\t}\n"
           "\t\t\"20\"\n\t\t{\n\t\t}\n\t}\n\t\"other\"\t\t\"unchanged \\\"quoted\\\" \\\\ value\"\n}\n";
}

// Edits both the bytes and a parsed tree, and checks they agree.
QByteArray edited(QByteArray data, const ValueEdit &edit)
{
    Node before;
    if (!vdf::parseText(data, before)) {
        return {};
    }
    QString error;
    if (!vdf::editText(data, edit, &error)) {
        qWarning() << error;
        return {};
    }
    Node after;
    if (!vdf::parseText(data, after)) {
        qWarning() << "edited text does not parse:" << data;
        return {};
    }
    vdf::applyEdit(before, edit);
    if (!vdf::sameTree(before, after)) {
        qWarning() << "edited text does not match the expected tree:" << data;
        return {};
    }
    return data;
}

} // namespace

class VdfEditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void replacesOnlyTheValue()
    {
        const QByteArray out = edited(sample(), {AppPath, QStringLiteral("launchoptions"), QStringLiteral("new value")});
        QVERIFY(!out.isEmpty());
        QByteArray expected = sample();
        expected.replace("\"LaunchOptions\"\t\t\"old\"", "\"LaunchOptions\"\t\t\"new value\"");
        QCOMPARE(out, expected);
    }

    void escapesWhatItWrites()
    {
        const QString value = QStringLiteral("A=\"x y\" C:\\path\tend\nnext");
        const QByteArray out = edited(sample(), {AppPath, QStringLiteral("LaunchOptions"), value});
        QVERIFY(!out.isEmpty());
        QVERIFY(out.contains("\"A=\\\"x y\\\" C:\\\\path\\tend\\nnext\""));
        Node tree;
        QVERIFY(vdf::parseText(out, tree));
        QCOMPARE(tree.find({u"Store", u"apps", u"10", u"LaunchOptions"})->value, value);
    }

    void addsAKeyToAnExistingSection()
    {
        const QByteArray out = edited(sample(),
                                      {{QStringLiteral("Store"), QStringLiteral("APPS"), QStringLiteral("20")},
                                       QStringLiteral("LaunchOptions"),
                                       QStringLiteral("x")});
        QVERIFY(!out.isEmpty());
        QVERIFY(out.contains("\t\t\"20\"\n\t\t{\n\t\t\t\"LaunchOptions\"\t\t\"x\"\n\t\t}"));
    }

    void createsMissingSections()
    {
        const QByteArray out = edited(sample(),
                                      {{QStringLiteral("Store"), QStringLiteral("apps"), QStringLiteral("30")},
                                       QStringLiteral("LaunchOptions"),
                                       QStringLiteral("x")});
        QVERIFY(!out.isEmpty());
        QVERIFY(out.contains("\t\t\"30\"\n\t\t{\n\t\t\t\"LaunchOptions\"\t\t\"x\"\n\t\t}"));

        const QByteArray deeper = edited(
            sample(), {{QStringLiteral("Store"), QStringLiteral("new"), QStringLiteral("deep")}, QStringLiteral("k"), QStringLiteral("v")});
        QVERIFY(!deeper.isEmpty());

        const QByteArray fromNothing =
            edited(QByteArray(), {{QStringLiteral("A"), QStringLiteral("B")}, QStringLiteral("k"), QStringLiteral("v")});
        QCOMPARE(fromNothing, QByteArray("\"A\"\n{\n\t\"B\"\n\t{\n\t\t\"k\"\t\t\"v\"\n\t}\n}\n"));
    }

    void removesTheWholeLine()
    {
        const QByteArray out = edited(sample(), {AppPath, QStringLiteral("LaunchOptions"), std::nullopt});
        QVERIFY(!out.isEmpty());
        QByteArray expected = sample();
        expected.replace("\t\t\t\"LaunchOptions\"\t\t\"old\"\n", "");
        QCOMPARE(out, expected);
    }

    void removesAPairThatSharesItsLine()
    {
        const QByteArray data = "\"S\" { \"a\" \"1\" \"b\" \"2\" }";
        const QByteArray out = edited(data, {{QStringLiteral("S")}, QStringLiteral("a"), std::nullopt});
        QCOMPARE(out, QByteArray("\"S\" {  \"b\" \"2\" }"));
    }

    void removingSomethingAbsentChangesNothing()
    {
        QCOMPARE(edited(sample(), {AppPath, QStringLiteral("Nope"), std::nullopt}), sample());
        QCOMPARE(edited(sample(), {{QStringLiteral("Nope")}, QStringLiteral("x"), std::nullopt}), sample());
    }

    void keepsWindowsLineEnds()
    {
        QByteArray data = sample();
        data.replace("\n", "\r\n");
        const QByteArray out = edited(data, {AppPath, QStringLiteral("LaunchOptions"), std::nullopt});
        QVERIFY(!out.isEmpty());
        QVERIFY(!out.contains("LaunchOptions"));
        QVERIFY(!out.contains("\r\r"));
    }

    void refusesDamagedInput()
    {
        QByteArray data = "\"Store\" { \"apps\" {";
        const QByteArray before = data;
        QString error;
        QVERIFY(!vdf::editText(data, {AppPath, QStringLiteral("k"), QStringLiteral("v")}, &error));
        QCOMPARE(data, before);
        QVERIFY(!error.isEmpty());
    }

    void editsFilesAtomically()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.vdf"));
        QVERIFY(test::writeFile(path, sample()));
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
        QString error;
        QVERIFY2(vdf::editTextFile(path,
                                   {{AppPath, QStringLiteral("LaunchOptions"), QStringLiteral("a")},
                                    {{QStringLiteral("Store"), QStringLiteral("apps"), QStringLiteral("20")},
                                     QStringLiteral("LaunchOptions"),
                                     QStringLiteral("b")}},
                                   &error),
                 qPrintable(error));
        Node tree;
        QVERIFY(vdf::readTextFile(path, tree));
        QCOMPARE(tree.find({u"Store", u"apps", u"10", u"LaunchOptions"})->value, QStringLiteral("a"));
        QCOMPARE(tree.find({u"Store", u"apps", u"20", u"LaunchOptions"})->value, QStringLiteral("b"));
        QCOMPARE(QFile::permissions(path) & test::FileModeBits, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("config.vdf")});

        QVERIFY(!vdf::editTextFile(dir.filePath(QStringLiteral("missing.vdf")), {}, &error));
    }

    void refusesAnEditThatWouldNotReadBack()
    {
        // Duplicate sections: the edit lands in the second copy, where the
        // check expects the first. Nothing must be written.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("dup.vdf"));
        const QByteArray data = "\"S\"\n{\n\t\"a\"\t\t\"1\"\n}\n\"S\"\n{\n\t\"b\"\t\t\"2\"\n}\n";
        QVERIFY(test::writeFile(path, data));
        QString error;
        QVERIFY(!vdf::editTextFile(path, {{{QStringLiteral("S")}, QStringLiteral("b"), QStringLiteral("changed")}}, &error));
        QVERIFY(error.contains(QLatin1String("nothing was written")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), data);
    }

    void survivesDamagedInputWithoutBreakingIt()
    {
        std::mt19937 random(5);
        for (int i = 0; i < 5000; ++i) {
            QByteArray data = test::mutate(sample(), random);
            Node before;
            const bool parses = vdf::parseText(data, before);
            const QByteArray original = data;
            const bool ok = vdf::editText(data, {AppPath, QStringLiteral("LaunchOptions"), QStringLiteral("x")});
            if (!ok) {
                QCOMPARE(data, original); // untouched on failure
                continue;
            }
            // Whatever editText accepted must still parse when the input did.
            Node after;
            if (parses) {
                QVERIFY(vdf::parseText(data, after));
            }
        }
    }
};

QTEST_GUILESS_MAIN(VdfEditTest)
#include "tst_vdfedit.moc"
