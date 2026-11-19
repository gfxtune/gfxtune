// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Loads the real window, off screen, on a made-up Steam library, and fails on
// any QML warning.

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "GpuMonitor.h"
#include "launch/LastLaunch.h"
#include "performance/Performance.h"
#include "support/FakeSteam.h"
#include "support/FakeSysfs.h"
#include "support/Fixtures.h"

#include <KLocalizedQmlContext>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest/quicktest.h>

#include <random>

#include <unistd.h>

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
    for (const QString &coverPath :
         {cache + QStringLiteral("1001/library_600x900.jpg"), cache + QStringLiteral("1002/") + test::NeonDriftCover}) {
        QVERIFY(QDir().mkpath(QFileInfo(coverPath).path()));
        QVERIFY(cover.save(coverPath, "JPEG"));
    }
}

// Lets the window finish its layout: the polish passes, and the zero-length
// timer Kirigami's form layout collects size changes with.
bool settle(QQuickWindow *window)
{
    for (int pass = 0; pass < 2; ++pass) {
        if (!QQuickTest::qWaitForPolish(window)) {
            return false;
        }
        QTest::qWait(10);
    }
    return QQuickTest::qWaitForPolish(window);
}

// An item as a failure message names it.
QString describe(const QQuickItem *item)
{
    const QString name = item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className()) : item->objectName();
    const QString text = item->property("text").toString();
    return text.isEmpty() ? name : QStringLiteral("%1 \"%2\"").arg(name, text.left(60));
}

// Where an item draws, left to right in its own coordinates: a text that
// neither wraps nor elides draws past its box when the box is too narrow.
std::pair<qreal, qreal> drawnSpan(const QQuickItem *item)
{
    const qreal width = item->width();
    const qreal painted = item->inherits("QQuickText") ? item->property("contentWidth").toReal() : 0;
    if (painted <= width) {
        return {0, width};
    }
    switch (item->property("effectiveHorizontalAlignment").toInt()) {
    case Qt::AlignRight:
        return {width - painted, width};
    case Qt::AlignHCenter:
        return {(width - painted) / 2, (width + painted) / 2};
    default:
        return {0, painted};
    }
}

// What a reader would see go wrong among the visible items: a text with a
// placeholder no argument filled in (or KI18n's mark for one), and anything
// reaching past the edge of the settings page, or of the window outside it.
// Returns how many items it looked at.
int findFaults(QQuickItem *item, QQuickItem *edge, QStringList &faults)
{
    if (!item->isVisible() || qFuzzyIsNull(item->opacity())) {
        return 0;
    }
    static const QRegularExpression unfilled(QStringLiteral("%\\d|I18N"));
    for (const char *property : {"text", "displayText", "placeholderText"}) {
        const QString text = item->property(property).toString();
        if (unfilled.match(text).hasMatch()) {
            faults << QStringLiteral("%1 reads \"%2\"").arg(describe(item), text);
        }
    }
    if (item->width() > 0 && item->height() > 0) {
        const auto [left, right] = drawnSpan(item);
        const qreal from = item->mapToItem(edge, QPointF(left, 0)).x();
        const qreal to = item->mapToItem(edge, QPointF(right, 0)).x();
        if (from < -0.5 || to > edge->width() + 0.5) {
            faults << QStringLiteral("%1 spans %2 to %3 of %4's %5")
                          .arg(describe(item))
                          .arg(from)
                          .arg(to)
                          .arg(describe(edge))
                          .arg(edge->width());
        }
    }
    if (item->objectName() == QLatin1String("gameSettingsPane")) {
        edge = item;
    }
    int seen = 1;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        seen += findFaults(child, edge, faults);
    }
    return seen;
}

// Visible settings whose "back to" button sits under the controls instead
// of beside them.
int stackedRows(QQuickItem *item)
{
    if (!item->isVisible()) {
        return 0;
    }
    int stacked = item->property("besideWidth").isValid() && item->property("columns").toInt() == 1 ? 1 : 0;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        stacked += stackedRows(child);
    }
    return stacked;
}

QQuickItem *findControl(QQuickItem *item, const char *type)
{
    if (item->inherits(type)) {
        return item;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findControl(child, type)) {
            return found;
        }
    }
    return nullptr;
}

// What a setting's control shows: a list's chosen value, a switch's state
// or a slider's position.
QVariant shownValue(QQuickItem *row)
{
    if (QQuickItem *box = findControl(row, "QQuickComboBox")) {
        return box->property("currentIndex").toInt() < 0 ? QVariant() : box->property("currentValue");
    }
    if (QQuickItem *control = findControl(row, "QQuickSwitch")) {
        return control->property("checked");
    }
    if (QQuickItem *slider = findControl(row, "QQuickSlider")) {
        return qRound(slider->property("value").toReal());
    }
    return {};
}

// Settings whose control shows something other than its row says, or
// whose row says something other than the editor holds while it can be
// changed (a locked switch may show "off" whatever the setting says).
// Returns how many it compared.
int findWrongValues(QQuickItem *item, const GameSettingsEditor &editor, QStringList &wrong)
{
    const QString setting = item->property("setting").toString();
    if (!setting.isEmpty() && item->property("controlEnabled").isValid()) {
        const QVariant shown = shownValue(item);
        const QVariant value = item->property("value");
        if (!shown.isValid() || shown.toInt() != value.toInt()) {
            wrong << QStringLiteral("%1 shows %2 instead of %3").arg(setting, shown.toString(), value.toString());
        }
        const QVariant held = editor.property(setting.toLatin1().constData());
        if (item->property("controlEnabled").toBool() && value.toInt() != held.toInt()) {
            wrong << QStringLiteral("%1 is %2 on the page, %3 in the editor").arg(setting, value.toString(), held.toString());
        }
        return 1;
    }
    const QString name = item->objectName();
    if (name == QLatin1String("frameCapSwitch") && item->property("checked") != editor.property("frameCapEnabled")) {
        wrong << QStringLiteral("frame rate cap switch shows %1").arg(item->property("checked").toString());
    }
    if (name == QLatin1String("frameCap") && item->property("value") != editor.property("frameCap")) {
        wrong << QStringLiteral("frame rate cap shows %1").arg(item->property("value").toString());
    }
    if (name == QLatin1String("upscalerSharpness") && qRound(item->property("value").toReal()) != editor.property("sharpness").toInt()) {
        wrong << QStringLiteral("upscaler sharpness shows %1").arg(item->property("value").toString());
    }
    int compared = name.startsWith(QLatin1String("frameCap")) || name == QLatin1String("upscalerSharpness") ? 1 : 0;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        compared += findWrongValues(child, editor, wrong);
    }
    return compared;
}

// Everything a page's settings say, as the editor holds them.
QVariantMap settingsOf(const GameSettingsEditor &editor)
{
    QVariantMap values;
    const QMetaObject *meta = editor.metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (property.isWritable()) {
            values.insert(QString::fromLatin1(property.name()), property.read(&editor));
        }
    }
    return values;
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
            KLocalization::setupLocalizedContext(&engine);
            QObject::connect(&engine, &QQmlEngine::warnings, &engine, [this](const QList<QQmlError> &list) { warnings += list; });
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
    void initTestCase() { QQuickStyle::setStyle(QStringLiteral("org.kde.desktop")); }

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
            QWheelEvent wheel(at, w.window->mapToGlobal(at), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(w.window, &wheel);
        }
        QTest::qWait(100);
        QCOMPARE(editor.verticalSync(), before);
        QVERIFY(!editor.customized());
    }

    void opensEveryPageAtTheTop()
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
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        editor.setAppId(1001);

        QQuickItem *pane = findItem(w.window->contentItem(), QStringLiteral("gameSettingsPane"));
        QVERIFY(pane);
        auto *view = pane->property("contentItem").value<QQuickItem *>();
        QVERIFY(view);
        QTRY_VERIFY(view->property("contentHeight").toReal() > view->height() + 300);
        const auto position = [view] { return view->property("contentY").toReal(); };

        // A rescan, as when Steam's files change, keeps the place.
        view->setProperty("contentY", 300);
        library.refresh();
        QVERIFY(QTest::qWaitFor([&] { return !library.scanning(); }, 10000));
        QCOMPARE(position(), 300.0);

        editor.setAppId(1002);
        QTRY_COMPARE(position(), 0.0);
        view->setProperty("contentY", 300);
        editor.selectGlobal();
        QTRY_COMPARE(position(), 0.0);
        QVERIFY(w.warnings.isEmpty());
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
        QVERIFY(test::writeFile(perf + QStringLiteral("/1003/live"),
                                "time=" + QByteArray::number(QDateTime::currentSecsSinceEpoch() + 30)
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
        QVERIFY(test::writeFile(perf + QStringLiteral("/1003/live"),
                                "time=" + QByteArray::number(QDateTime::currentSecsSinceEpoch())
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

        QTest::mouseClick(w.window, Qt::LeftButton, {},
                          allGames->mapToScene(QPointF(allGames->width() / 2, allGames->height() / 2)).toPoint());
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
        QTest::mouseClick(w.window, Qt::LeftButton, {},
                          usesGlobal->mapToScene(QPointF(usesGlobal->height() / 2, usesGlobal->height() / 2)).toPoint());
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

    void showsEveryPageWhole_data()
    {
        QTest::addColumn<bool>("installed");
        QTest::addColumn<bool>("steamOpen");
        QTest::newRow("tools installed, Steam closed") << true << false;
        QTest::newRow("tools installed, a game running") << true << true;
        QTest::newRow("nothing installed, Steam closed") << false << false;
        QTest::newRow("nothing installed, a game running") << false << true;
    }

    // Every page, in states that bring their own notes and blocks, at the
    // smallest and the usual window width: every text filled in, and nothing
    // past the edge of the page or the window.
    void showsEveryPageWhole()
    {
        QFETCH(bool, installed);
        QFETCH(bool, steamOpen);
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        const QString perf = performance::performanceRoot(store.baseDir());

        // A measured session and a last launch for 1001, with the report's
        // real texts; 1003 seen drawing with OpenGL.
        QVERIFY(test::writeFile(perf + QStringLiteral("/1001/history"),
                                "1789990000,3900.0,468000,120.00,95.40,80.60,60.4,74.6,88.0,210.4,97.0\n"));
        settings::GameSettings launched;
        launched.anisotropicFiltering = 16;
        launched.frameCapEnabled = true;
        launched.frameCap = 90;
        launched.verticalSync = settings::VerticalSync::Fast;
        launched.gamescope = true;
        launched.renderScale = 150;
        launched.filters.sharpening = 40;
        launched.gameMode = true;
        launched.mangoHud = true;
        launch::LaunchContext context;
        context.environment.insert(QStringLiteral("DXVK_FRAME_RATE"), QStringLiteral("30"));
        context.screen = launch::Screen{1920, 1080, 144};
        context.proton = true;
        context.gamescopeInstalled = installed;
        context.gameModeInstalled = installed;
        context.mangoHudInstalled = installed;
        const launch::LaunchPlan plan = launch::planLaunch(launched, context);
        QVERIFY(!plan.applied.isEmpty());
        QVERIFY(launch::writeLastLaunch(store.baseDir(), 1001, plan, QDateTime::currentDateTime()));
        settings::GameFacts openGl;
        openGl.openGlSeen = true;
        QVERIFY(store.saveFacts(1003, openGl, QStringLiteral("Stonewake")));
        if (steamOpen) {
            const QByteArray owner = "Uid:\t" + QByteArray::number(getuid()) + "\t0\t0\t0\n";
            QVERIFY(test::writeFile(proc.filePath(QStringLiteral("4242/comm")), "steam\n"));
            QVERIFY(test::writeFile(proc.filePath(QStringLiteral("4242/status")), owner));
            QVERIFY(test::writeFile(proc.filePath(QStringLiteral("4243/comm")), "reaper\n"));
            QVERIFY(test::writeFile(proc.filePath(QStringLiteral("4243/status")), owner));
            QVERIFY(test::writeFile(proc.filePath(QStringLiteral("4243/cmdline")), QByteArray("reaper\0SteamLaunch\0AppId=1001\0", 30)));
            QVERIFY(test::writeFile(perf + QStringLiteral("/1001/live"),
                                    "time=" + QByteArray::number(QDateTime::currentSecsSinceEpoch())
                                        + "\nfps=88\nframe_ms=11.4\nframe_ms_max=19\n"));
        }

        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        library.setPollInterval(100);
        GameSettingsEditor editor(&library, &store, installed ? settings::SystemInfo{true, true, true, true} : settings::SystemInfo{});
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path(), nullptr, 100);
        gpu.setGameSources(perf, proc.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));
        QTRY_COMPARE(library.steamRunning(), steamOpen);

        // Changes that bring rows, notes and "back to" buttons: global ones,
        // gamescope's rows with supersampling, the filters, a game out of
        // the global settings.
        editor.selectGlobal();
        editor.setAnisotropicFiltering(8);
        editor.setGameMode(true);
        editor.setAppId(1001);
        editor.setGamescope(true);
        editor.setRenderScale(150);
        editor.setFrameCapEnabled(true);
        editor.setFilterSharpening(40);
        editor.setFilterTemperature(-30);
        editor.setAntiAliasing(1);
        editor.setAppId(1002);
        editor.setUsesGlobal(false);
        editor.setLodBias(-50);
        editor.setVerticalSync(int(settings::VerticalSync::Adaptive));
        editor.setAppId(1005);
        editor.setGamescope(true);
        editor.setRenderScale(50);
        editor.setMangoHud(true);

        QList<int> pages{0};
        for (int row = 0; row < library.filtered()->rowCount(); ++row) {
            pages << library.filtered()->appIdAt(row);
        }
        QVERIFY(pages.size() > 5);
        const int usualWidth = w.window->width();
        for (const int width : {w.window->minimumWidth(), usualWidth}) {
            w.window->resize(width, w.window->height());
            for (const int page : std::as_const(pages)) {
                page == 0 ? editor.selectGlobal() : editor.setAppId(page);
                QVERIFY(settle(w.window));
                QStringList faults;
                if (w.window->title().contains(QLatin1Char('%'))) {
                    faults << QStringLiteral("the title reads \"%1\"").arg(w.window->title());
                }
                QVERIFY(findFaults(w.window->contentItem(), w.window->contentItem(), faults) > 200);
                // The launch line, longer than its box here, shows its start.
                const QQuickItem *line = findItem(w.window->contentItem(), QStringLiteral("launchLine"));
                QVERIFY(line);
                if (line->isVisible() && line->property("cursorPosition").toInt() != 0) {
                    faults << QStringLiteral("the launch line shows from character %1").arg(line->property("cursorPosition").toInt());
                }
                // With room to spare, every "back to" button sits beside its setting.
                if (width == usualWidth && stackedRows(w.window->contentItem()) > 0) {
                    faults << QStringLiteral("%1 settings stacked with room to spare").arg(stackedRows(w.window->contentItem()));
                }
                QVERIFY2(faults.isEmpty(),
                         qPrintable(QStringLiteral("%1, %2 px wide, %3 faults:\n").arg(w.window->title()).arg(width).arg(faults.size())
                                    + faults.mid(0, 8).join(QLatin1Char('\n'))));
            }
        }
        QVERIFY2(w.warnings.isEmpty(), qPrintable(w.describeWarnings()));
    }

    // Pages picked faster than the window draws, with settings changed in
    // between: every control shows the selected page's value, and every
    // change is kept for the game it was made on.
    void showsTheRightValuesWhileSwitchingQuickly()
    {
        test::FakeSteam steam;
        steam.build();
        writeRealCovers(steam);
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        const settings::SystemInfo system{true, true, true, true};
        GameSettingsEditor editor(&library, &store, system);
        QTemporaryDir sys;
        test::addCard(sys.path(), 0);
        GpuMonitor gpu(sys.path());

        Window w(library, editor, gpu);
        QVERIFY(w.window);
        w.window->show();
        w.window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w.window));
        library.start();
        QVERIFY(QTest::qWaitFor([&] { return library.loaded() && !library.scanning(); }, 10000));

        // Different values on every page.
        editor.selectGlobal();
        editor.setAnisotropicFiltering(4);
        editor.setGameMode(true);
        editor.setAppId(1001);
        editor.setAnisotropicFiltering(16);
        editor.setLodBias(-50);
        editor.setVerticalSync(int(settings::VerticalSync::Fast));
        editor.setFilterSharpening(60);
        editor.setFilterGamma(140);
        editor.setAppId(1002);
        editor.setGamescope(true);
        editor.setRenderScale(150);
        editor.setUpscaleFilter(int(settings::UpscaleFilter::Nis));
        editor.setScalingMode(int(settings::ScalingMode::Fit));
        editor.setFrameCapEnabled(true);
        editor.setFrameCap(90);
        editor.setAppId(1003);
        editor.setAntiAliasing(1);
        editor.setMeasurePerformance(false);
        editor.setMangoHud(true);
        editor.setAppId(1005);
        editor.setUsesGlobal(false);
        editor.setFiltersEnabled(false);

        QQuickItem *pane = findItem(w.window->contentItem(), QStringLiteral("gameSettingsPane"));
        QVERIFY(pane);
        const QList<int> pages{0, 1001, 1002, 1003, 1004, 1005};
        QHash<int, QVariantMap> lastSeen; // each game page as it last showed
        const auto select = [&](int page) { page == 0 ? editor.selectGlobal() : editor.setAppId(page); };
        // A change made the way a user makes it, on the game page that is
        // open; the global page stays as it is, so every game's last look
        // stays valid.
        int picks = 0;
        const auto pick = [&](const char *name, Qt::Key key) {
            QQuickItem *control = findItem(pane, QString::fromLatin1(name));
            if (control && control->isEnabled() && !editor.isGlobal()) {
                const QVariantMap before = settingsOf(editor);
                control->forceActiveFocus();
                QTest::keyClick(w.window, key);
                picks += settingsOf(editor) != before ? 1 : 0;
                lastSeen.insert(editor.appId(), settingsOf(editor));
            }
            return control != nullptr;
        };

        std::mt19937 random(7); // fixed: a failure can be repeated
        std::uniform_int_distribution<qsizetype> anyPage(0, pages.size() - 1);
        for (int step = 0; step < 60; ++step) {
            // Up to three pages before the window gets to draw.
            const int burst = 1 + int(random() % 3);
            for (int i = 0; i < burst; ++i) {
                select(pages.at(anyPage(random)));
            }
            if (step % 4 == 1) {
                QVERIFY(pick("gameMode", Qt::Key_Space));
                select(pages.at(anyPage(random)));
            } else if (step % 4 == 3) {
                QVERIFY(pick("verticalSync", Qt::Key_Down));
            }
            QVERIFY(settle(w.window));
            const QString title =
                editor.isGlobal() ? QStringLiteral("All Games — gfxtune") : editor.gameName() + QStringLiteral(" — gfxtune");
            QCOMPARE(w.window->title(), title);
            QStringList wrong;
            QCOMPARE(findWrongValues(pane, editor, wrong), 23);
            QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("step %1, %2:\n").arg(step).arg(title) + wrong.join(QLatin1Char('\n'))));
            if (!editor.isGlobal()) {
                lastSeen.insert(editor.appId(), settingsOf(editor));
            }
        }
        QVERIFY(picks >= 10);

        // Read back from the saved files, every game is as it last showed.
        editor.flush();
        GameSettingsEditor reread(&library, &store, system);
        QCOMPARE(lastSeen.size(), pages.size() - 1);
        for (auto it = lastSeen.cbegin(); it != lastSeen.cend(); ++it) {
            reread.setAppId(it.key());
            const QVariantMap saved = settingsOf(reread);
            for (auto value = it->cbegin(); value != it->cend(); ++value) {
                QVERIFY2(saved.value(value.key()) == value.value(),
                         qPrintable(QStringLiteral("%1 of %2: saved %3, shown %4")
                                        .arg(value.key())
                                        .arg(it.key())
                                        .arg(saved.value(value.key()).toString(), value->toString())));
            }
        }
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
