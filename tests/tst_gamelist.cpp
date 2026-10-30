// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameListModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace gfxtune;
using steam::Game;
using steam::Runtime;

namespace
{

Game game(quint32 id, const QString &name)
{
    Game g;
    g.appId = id;
    g.name = name;
    g.runtime = Runtime::Proton;
    return g;
}

QList<quint32> ids(const GameListModel &model)
{
    QList<quint32> out;
    for (int row = 0; row < model.rowCount(); ++row) {
        out.append(quint32(model.index(row).data(GameListModel::AppIdRole).toInt()));
    }
    return out;
}

} // namespace

class GameListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void updatesInPlaceAndKeepsRows()
    {
        GameListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removes(&model, &QAbstractItemModel::rowsRemoved);

        model.setGames({game(1, "A"), game(3, "C")});
        QCOMPARE(ids(model), (QList<quint32>{1, 3}));

        // A game installed in between: one insert, no reset.
        model.setGames({game(1, "A"), game(2, "B"), game(3, "C")});
        QCOMPARE(ids(model), (QList<quint32>{1, 2, 3}));
        QCOMPARE(resets.count(), 0);

        // One uninstalled, one added at the end.
        model.setGames({game(1, "A"), game(3, "C"), game(4, "D")});
        QCOMPARE(ids(model), (QList<quint32>{1, 3, 4}));
        QCOMPARE(resets.count(), 0);
        QCOMPARE(removes.count(), 1);

        // Details change without any rows moving.
        Game renamedDetail = game(3, "C");
        renamedDetail.runtime = Runtime::Native;
        model.setGames({game(1, "A"), renamedDetail, game(4, "D")});
        QCOMPARE(model.index(1).data(GameListModel::DetailRole).toString(), QStringLiteral("Native"));
        QCOMPARE(resets.count(), 0);

        // A rename that changes the order falls back to a reset.
        model.setGames({game(4, "0 first now"), game(1, "A"), game(3, "C")});
        QCOMPARE(ids(model), (QList<quint32>{4, 1, 3}));
        QCOMPARE(resets.count(), 1);

        model.setGames({});
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(inserts.count() >= 3);
    }

    void exposesRoles()
    {
        GameListModel model;
        Game g = game(7, "Neon Drift");
        g.coverPath = QStringLiteral("/covers/7.jpg");
        g.tool.displayName = QStringLiteral("GE-Proton10-26");
        model.setGames({g});
        const QModelIndex i = model.index(0);
        QCOMPARE(i.data(GameListModel::NameRole).toString(), QStringLiteral("Neon Drift"));
        QCOMPARE(i.data(GameListModel::DetailRole).toString(), QStringLiteral("GE-Proton10-26"));
        QCOMPARE(i.data(GameListModel::CoverRole).toUrl(), QUrl::fromLocalFile(QStringLiteral("/covers/7.jpg")));
        QCOMPARE(i.data(GameListModel::SettingsNoteRole).toString(), QString());
        QCOMPARE(i.data(GameListModel::RunningRole).toBool(), false);

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setRunning({7});
        model.setSettingsNotes({{7, QStringLiteral("2 changes")}});
        QCOMPARE(changed.count(), 2);
        QCOMPARE(i.data(GameListModel::RunningRole).toBool(), true);
        QCOMPARE(i.data(GameListModel::SettingsNoteRole).toString(), QStringLiteral("2 changes"));
        model.setRunning({7}); // no change, no signal
        QCOMPARE(changed.count(), 2);

        QVERIFY(!model.index(5).data(GameListModel::NameRole).isValid());
        QCOMPARE(model.find(7)->name, QStringLiteral("Neon Drift"));
        QVERIFY(!model.find(8));
    }

    void describesHowGamesRun()
    {
        Game g = game(1, "x");
        QCOMPARE(GameListModel::detailText(g), QStringLiteral("Proton"));
        g.runtime = Runtime::Native;
        QCOMPARE(GameListModel::detailText(g), QStringLiteral("Native"));
        g.runtime = Runtime::Unknown;
        QCOMPARE(GameListModel::detailText(g), QStringLiteral("Unknown"));
    }

    void filtersBySearchText()
    {
        GameListModel model;
        model.setGames({game(1, "Ashfall Protocol"), game(2, "Neon Drift"), game(3, "Ashfall Soundtrack Edition")});
        GameFilterModel filtered;
        filtered.setSourceModel(&model);
        QAbstractItemModelTester tester(&filtered, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&filtered, &GameFilterModel::filterTextChanged);

        filtered.setFilterText(QStringLiteral("  ashFALL "));
        QCOMPARE(filtered.rowCount(), 2);
        QCOMPARE(filtered.appIdAt(0), 1);
        QCOMPARE(filtered.rowOf(3), 1);
        QCOMPARE(filtered.rowOf(2), -1);
        QCOMPARE(filtered.appIdAt(9), 0);

        filtered.setFilterText(QStringLiteral("  ashFALL "));
        QCOMPARE(changed.count(), 1);
        filtered.setFilterText({});
        QCOMPARE(filtered.rowCount(), 3);

        // The filter follows the list as it changes.
        filtered.setFilterText(QStringLiteral("drift"));
        model.setGames({game(1, "Ashfall Protocol"), game(4, "Drift Kings"), game(2, "Neon Drift")});
        QCOMPARE(filtered.rowCount(), 2);
    }
};

QTEST_GUILESS_MAIN(GameListTest)
#include "tst_gamelist.moc"
