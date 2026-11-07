// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "support/Fixtures.h"
#include "vdf/TextVdf.h"
#include "vdf/TextVdfEdit.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <random>

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

    // Many edits in one pass give the same bytes as making them one by one,
    // for every kind of edit, and the file check accepts the result.
    void editsManyValuesAsIfOneByOne()
    {
        const QStringList apps{QStringLiteral("Store"), QStringLiteral("apps")};
        const auto app = [&apps](const char *id) { return apps + QStringList{QString::fromLatin1(id)}; };
        const QList<ValueEdit> edits{
            {AppPath, QStringLiteral("LaunchOptions"), QStringLiteral("replaced")},
            {AppPath, QStringLiteral("LastPlayed"), std::nullopt},                // a line of its own
            {app("20"), QStringLiteral("LaunchOptions"), QStringLiteral("added")}, // into an empty section
            {app("30"), QStringLiteral("LaunchOptions"), QStringLiteral("new section")},
            {app("40"), QStringLiteral("LaunchOptions"), QStringLiteral("another")},
            {{QStringLiteral("Store")}, QStringLiteral("other"), QStringLiteral("top")},
            {{QStringLiteral("Store"), QStringLiteral("new")}, QStringLiteral("a"), QStringLiteral("1")}, // both create "new"
            {{QStringLiteral("Store"), QStringLiteral("new")}, QStringLiteral("b"), QStringLiteral("2")},
            {{QStringLiteral("Later")}, QStringLiteral("x"), QStringLiteral("1")}, // new top-level sections
            {{QStringLiteral("Last")}, QStringLiteral("y"), QStringLiteral("2")},
            {app("50"), QStringLiteral("gone"), std::nullopt}, // nothing to remove
        };
        QByteArray oneByOne = sample();
        for (const ValueEdit &edit : edits) {
            oneByOne = edited(oneByOne, edit);
            QVERIFY(!oneByOne.isEmpty());
        }
        QByteArray together = sample();
        QString error;
        QVERIFY2(vdf::editText(together, edits, &error), qPrintable(error));
        QCOMPARE(together, oneByOne);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.vdf"));
        QVERIFY(test::writeFile(path, sample()));
        QVERIFY2(vdf::editTextFile(path, edits, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), oneByOne);
    }

    void createsASectionOnlyOnce()
    {
        QByteArray data = "\"Store\"\n{\n}\n";
        const QStringList apps{QStringLiteral("Store"), QStringLiteral("apps")};
        QVERIFY(vdf::editText(data,
                              {{apps + QStringList{QStringLiteral("1")}, QStringLiteral("LaunchOptions"), QStringLiteral("a")},
                               {apps + QStringList{QStringLiteral("2")}, QStringLiteral("LaunchOptions"), QStringLiteral("b")}}));
        Node tree;
        QVERIFY(vdf::parseText(data, tree));
        const Node *store = tree.child(u"Store");
        QVERIFY(store);
        QCOMPARE(std::ranges::count_if(store->children, [](const Node &n) { return n.key == QLatin1String("apps"); }), 1);
        QCOMPARE(tree.find({u"Store", u"apps", u"1", u"LaunchOptions"})->value, QStringLiteral("a"));
        QCOMPARE(tree.find({u"Store", u"apps", u"2", u"LaunchOptions"})->value, QStringLiteral("b"));
    }

    void aLaterEditOfTheSameValueWins()
    {
        QByteArray data = sample();
        QVERIFY(vdf::editText(data,
                              {{AppPath, QStringLiteral("LaunchOptions"), QStringLiteral("first")},
                               {AppPath, QStringLiteral("launchoptions"), QStringLiteral("second")}}));
        Node tree;
        QVERIFY(vdf::parseText(data, tree));
        QCOMPARE(tree.find({u"Store", u"apps", u"10", u"LaunchOptions"})->value, QStringLiteral("second"));
        QVERIFY(!data.contains("first"));
    }

    // Random batches on random files: the same bytes as one edit at a time.
    void batchesMatchSingleEditsOnRandomInput()
    {
        std::mt19937 random(11);
        const QList<ValueEdit> pool{
            {AppPath, QStringLiteral("LaunchOptions"), QStringLiteral("x")},
            {AppPath, QStringLiteral("LaunchOptions"), std::nullopt},
            {AppPath, QStringLiteral("LastPlayed"), std::nullopt},
            {{QStringLiteral("Store"), QStringLiteral("apps"), QStringLiteral("20")}, QStringLiteral("k"), QStringLiteral("v")},
            {{QStringLiteral("Store"), QStringLiteral("apps"), QStringLiteral("99")}, QStringLiteral("k"), QStringLiteral("v")},
            {{QStringLiteral("Store"), QStringLiteral("more")}, QStringLiteral("a"), QStringLiteral("1")},
            {{QStringLiteral("Store"), QStringLiteral("more")}, QStringLiteral("b"), QStringLiteral("2")},
            {{QStringLiteral("Top")}, QStringLiteral("t"), QStringLiteral("1")},
            {{QStringLiteral("Store")}, QStringLiteral("other"), std::nullopt},
        };
        for (int i = 0; i < 2000; ++i) {
            const QByteArray input = i % 4 == 0 ? test::mutate(sample(), random) : sample();
            QList<ValueEdit> edits;
            const int count = int(random() % 6) + 1;
            for (int k = 0; k < count; ++k) {
                edits.append(pool[qsizetype(random() % std::uint32_t(pool.size()))]);
            }
            // Later edits of a value replace earlier ones in their place.
            QList<ValueEdit> merged;
            for (const ValueEdit &edit : edits) {
                const auto same = std::ranges::find_if(merged, [&edit](const ValueEdit &m) {
                    return m.sectionPath == edit.sectionPath && m.key.compare(edit.key, Qt::CaseInsensitive) == 0;
                });
                if (same != merged.end()) {
                    *same = edit;
                } else {
                    merged.append(edit);
                }
            }
            QByteArray together = input;
            QByteArray oneByOne = input;
            bool ok = true;
            for (const ValueEdit &edit : merged) {
                ok = ok && vdf::editText(oneByOne, edit);
            }
            QCOMPARE(vdf::editText(together, edits), ok);
            if (ok) {
                QCOMPARE(together, oneByOne);
            } else {
                QCOMPARE(together, input); // untouched on failure
            }
        }
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
