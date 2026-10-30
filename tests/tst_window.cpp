// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Loads the real window, off screen, on a made-up Steam library, and fails on
// any QML warning.

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "GpuMonitor.h"
#include "performance/Performance.h"
#include "support/FakeSteam.h"
#include "support/FakeSysfs.h"
#include "support/Fixtures.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlListReference>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Gfxtune_BackendPlugin)
Q_IMPORT_QML_PLUGIN(GfxtunePlugin)

using namespace gfxtune;

namespace
{

// Searches the visual tree, which is where pages and layouts put their items.
QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == name) {
        return item;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// The charts drawing at least `points` readings, among the visible items.
int chartsWithData(QQuickItem *item, int points)
{
    if (!item || !item->isVisible()) {
        return 0;
    }
    int count = 0;
    if (QByteArray(item->metaObject()->className()) == "LineChart") {
        const QQmlListReference sources(item, "valueSources");
        if (sources.count() > 0 && sources.at(0)->property("array").toList().size() >= points && item->width() > 0) {
            ++count;
        }
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        count += chartsWithData(child, points);
    }
    return count;
}

// Real images, since the window decodes them: one in each layout.
void writeRealCovers(const test::FakeSteam &steam)
{
    QImage cover(60, 90, QImage::Format_RGB32);
    cover.fill(QColor(40, 90, 160));
    const QString cache = steam.root + QStringLiteral("/appcache/librarycache/");
    for (const QString &coverPath : {cache + QStringLiteral("1001/library_600x900.jpg"), cache + QStringLiteral("1002/") + test::NeonDriftCover}) {
        QVERIFY(QDir().mkpath(QFileInfo(coverPath).path()));
        QVERIFY(cover.save(coverPath, "JPEG"));
    }
}

} // namespace

class WindowTest : public QObject
{
    Q_OBJECT

private:
    struct Window {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        QQuickWindow *window = nullptr;

        Window(GameLibrary &library, GameSettingsEditor &editor, GpuMonitor &gpu)
        {
            QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                             [this](const QList<QQmlError> &list) { warnings += list; });
            engine.setInitialProperties({
                {QStringLiteral("library"), QVariant::fromValue(&library)},
                {QStringLiteral("editor"), QVariant::fromValue(&editor)},
                {QStringLiteral("gpu"), QVariant::fromValue(&gpu)},
            });
            engine.loadFromModule("Gfxtune", "Main");
            if (!engine.rootObjects().isEmpty()) {
                window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            }
        }

        QString describeWarnings() const
        {
            QStringList lines;
            for (const QQmlError &e : warnings) {
                lines << e.toString();
            }
            return lines.join(QLatin1Char('\n'));
        }
    };

private Q_SLOTS:
    void initTestCase()
    {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    void showsALibraryAndSelectsTheFirstGame()
    {
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY2(w.window, qPrintable(w.describeWarnings()));
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        QTRY_COMPARE(editor.appId(), library.filtered()->appIdAt(0));
        QVERIFY(editor.hasGame());

        // Exercise the page: switch games, search, toggle gamescope.
        editor.setAppId(1002);
        editor.setGamescope(true);
        library.filtered()->setFilterText(QStringLiteral("drift"));
        QTest::qWait(300);
        library.filtered()->setFilterText({});
        editor.setAppId(1003);
        QTest::qWait(300);

        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    void scrollingNeverChangesASetting()
    {
        test::FakeSteam steam;
        steam.build();
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        editor.setAppId(1001);

        QQuickItem *box = findItem(w.window->contentItem(), QStringLiteral("verticalSync"));
        QVERIFY(box);
        QTRY_VERIFY(box->isVisible() && box->width() > 0);
        const int before = editor.verticalSync();
        const QPointF at = box->mapToScene(QPointF(box->width() / 2, box->height() / 2));
        for (int i = 0; i < 6; ++i) {
            QWheelEvent wheel(at, w.window->mapToGlobal(at), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(w.window, &wheel);
        }
        QTest::qWait(100);
        QCOMPARE(editor.verticalSync(), before);
        QVERIFY(!editor.customized());
    }

    void locksTheFiltersBehindTheWarning()
    {
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));

        // A game without anti-cheat: the filters are there to use.
        editor.setAppId(1001);
        QQuickItem *warning = findItem(w.window->contentItem(), QStringLiteral("filtersWarning"));
        QQuickItem *sharpening = findItem(w.window->contentItem(), QStringLiteral("filterSharpening"));
        QVERIFY(warning && sharpening);
        QTRY_VERIFY(!warning->isVisible());
        QVERIFY(sharpening->isEnabled());

        // Easy Anti-Cheat: warned and locked until accepted.
        editor.setAppId(1004);
        QTRY_VERIFY(warning->isVisible());
        QVERIFY(!sharpening->isEnabled());
        editor.acceptFiltersRisk();
        QTRY_VERIFY(!warning->isVisible());
        QVERIFY(sharpening->isEnabled());

        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    // The game page's performance history and the panel's block for the
    // game being measured.
    void showsMeasuringAndTheGameBeingPlayed()
    {
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        const QString perf = performance::performanceRoot(store.baseDir());
        QVERIFY(test::writeFile(perf + QStringLiteral("/1003/history"),
                                "1789990000,3900.0,468000,120.00,95.40,80.60,60.4,74.6,88.0,210.4,97.0\n"));
        QVERIFY(test::writeFile(perf + QStringLiteral("/1003/live"), "time=" + QByteArray::number(QDateTime::currentSecsSinceEpoch() + 30)
                                                                         + "\nfps=0\n")); // the clock jumped: not shown
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path(), nullptr, 100);
        gpu.setGameSources(perf, proc.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        editor.setAppId(1003);

        QQuickItem *content = w.window->contentItem();
        for (const char *name : {"lodBias", "antiAliasing", "gameMode", "mangoHud", "cpuReadings", "heldBackBy"}) {
            QVERIFY2(findItem(content, QString::fromLatin1(name)), name);
        }
        QQuickItem *measure = findItem(content, QStringLiteral("measurePerformance"));
        QQuickItem *table = findItem(content, QStringLiteral("sessionTable"));
        QQuickItem *game = findItem(content, QStringLiteral("gameBlock"));
        QVERIFY(measure && table && game);
        QTRY_VERIFY(table->isVisible());
        QVERIFY(measure->isEnabled());
        QVERIFY(measure->property("checked").toBool());
        QVERIFY(!game->isVisible());

        // The header and one session as a table while it fits; in the
        // smallest window a list instead.
        const auto cells = [table] {
            int count = 0;
            for (QQuickItem *cell : table->childItems()) {
                count += cell->isVisible() && cell->property("text").isValid() ? 1 : 0;
            }
            return count;
        };
        QTRY_COMPARE(cells(), 16);
        QQuickItem *pane = findItem(content, QStringLiteral("gameSettingsPane"));
        QVERIFY(pane);
        w.window->resize(w.window->minimumWidth(), w.window->height());
        QTRY_COMPARE(w.window->width(), w.window->minimumWidth());
        QQuickItem *list = findItem(content, QStringLiteral("sessionList"));
        QVERIFY(list);
        QTRY_VERIFY(list->isVisible());
        QVERIFY(!table->isVisible());
        QVERIFY2(list->mapToItem(pane, QPointF(list->width(), 0)).x() <= pane->width(),
                 qPrintable(QStringLiteral("list %1 wide, page %2").arg(list->width()).arg(pane->width())));
        w.window->resize(w.window->minimumWidth() * 3 / 2, w.window->height());
        QTRY_VERIFY(table->isVisible());
        QVERIFY(!list->isVisible());
        QCOMPARE(cells(), 16);
        QVERIFY(table->mapToItem(pane, QPointF(table->width(), 0)).x() <= pane->width());

        // A game being played shows in the panel.
        QVERIFY(test::writeFile(perf + QStringLiteral("/1003/live"), "time=" + QByteArray::number(QDateTime::currentSecsSinceEpoch())
                                                                         + "\nfps=88\nframe_ms=11.4\nframe_ms_max=19\n"));
        QTRY_VERIFY(game->isVisible());
        QCOMPARE(gpu.gameFps(), QStringLiteral("88 FPS"));

        // Measuring is locked with the filters in a game with anti-cheat.
        editor.setAppId(1004);
        QTRY_VERIFY(!measure->isEnabled());
        QVERIFY(!measure->property("checked").toBool());
        editor.acceptFiltersRisk();
        QTRY_VERIFY(measure->isEnabled());
        QVERIFY(measure->property("checked").toBool());

        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    // "All Games" opens the global page; on a game's page a changed setting
    // offers the global value back.
    void showsGlobalSettingsAndAGamesChanges()
    {
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        QTRY_VERIFY(editor.hasGame());

        QQuickItem *content = w.window->contentItem();
        QQuickItem *allGames = findItem(content, QStringLiteral("allGames"));
        QQuickItem *header = findItem(content, QStringLiteral("globalHeader"));
        QQuickItem *usesGlobal = findItem(content, QStringLiteral("usesGlobal"));
        QQuickItem *backTo = findItem(content, QStringLiteral("backTo:anisotropicFiltering"));
        QQuickItem *history = findItem(content, QStringLiteral("sessionTable"));
        QVERIFY(allGames && header && usesGlobal && backTo && history);
        QTRY_VERIFY(allGames->isVisible() && allGames->width() > 0);
        QVERIFY(!header->isVisible());
        QVERIFY(usesGlobal->isVisible());

        QTest::mouseClick(w.window, Qt::LeftButton, {}, allGames->mapToScene(QPointF(allGames->width() / 2, allGames->height() / 2)).toPoint());
        QTRY_VERIFY(editor.isGlobal());
        QTRY_VERIFY(header->isVisible());
        QVERIFY(!usesGlobal->isVisible());
        QVERIFY(allGames->property("highlighted").toBool());
        QCOMPARE(w.window->title(), QStringLiteral("All Games — gfxtune"));
        editor.setAnisotropicFiltering(16);
        QVERIFY(!backTo->isVisible()); // nothing to go back to here

        editor.setAppId(1003);
        QTRY_VERIFY(!header->isVisible());
        QVERIFY(!allGames->property("highlighted").toBool());
        QVERIFY(!backTo->isVisible());
        editor.setAnisotropicFiltering(4);
        QTRY_VERIFY(backTo->isVisible());
        QCOMPARE(backTo->property("text").toString(), QStringLiteral("Global: 16×"));
        QTest::mouseClick(w.window, Qt::LeftButton, {}, backTo->mapToScene(QPointF(backTo->width() / 2, backTo->height() / 2)).toPoint());
        QTRY_COMPARE(editor.anisotropicFiltering(), 16);
        QTRY_VERIFY(!backTo->isVisible());

        // Out of the global settings, a change is measured against "left alone".
        QTest::mouseClick(w.window, Qt::LeftButton, {}, usesGlobal->mapToScene(QPointF(usesGlobal->height() / 2, usesGlobal->height() / 2)).toPoint());
        QTRY_VERIFY(!editor.usesGlobal());
        QCOMPARE(editor.anisotropicFiltering(), 0);
        editor.setAnisotropicFiltering(8);
        QTRY_VERIFY(backTo->isVisible());
        QCOMPARE(backTo->property("text").toString(), QStringLiteral("Without gfxtune: Game decides"));

        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    void readsTheCardOnlyWhileVisible()
    {
        test::FakeSteam steam;
        steam.build();
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path(), nullptr, 50);

        Window w(library, editor, gpu);
        QVERIFY2(w.window, qPrintable(w.describeWarnings()));
        w.window->show();
        QTRY_VERIFY(gpu.active());
        QTRY_VERIFY(gpu.edgeHistory().size() >= 2);
        // Temperature, hotspot and power lines draw the readings.
        QTRY_COMPARE(chartsWithData(w.window->contentItem(), 2), 3);
        w.window->showMinimized();
        QTRY_VERIFY(!gpu.active());
        w.window->showNormal();
        QTRY_VERIFY(gpu.active());
        QTest::qWait(200);
        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    void showsWhenSteamIsMissing()
    {
        QTemporaryDir home;
        QTemporaryDir config;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        steam::ScanOptions options;
        options.homePath = home.path();
        GameLibrary library(options, &store);
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{});
        QTemporaryDir sys; // no graphics card at all
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY2(w.window, qPrintable(w.describeWarnings()));
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        QTest::qWait(200);
        QVERIFY(!editor.hasGame());
        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }
};

int main(int argc, char *argv[])
{
    // No display in tests.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    WindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_window.moc"
