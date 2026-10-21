// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Loads the real window, off screen, on a made-up Steam library, and fails on
// any QML warning.

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "support/FakeSteam.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
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

} // namespace

class WindowTest : public QObject
{
    Q_OBJECT

private:
    struct Window {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        QQuickWindow *window = nullptr;

        Window(GameLibrary &library, GameSettingsEditor &editor)
        {
            QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                             [this](const QList<QQmlError> &list) { warnings += list; });
            engine.setInitialProperties({
                {QStringLiteral("library"), QVariant::fromValue(&library)},
                {QStringLiteral("editor"), QVariant::fromValue(&editor)},
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
        // A real image, since the window decodes it.
        QImage cover(60, 90, QImage::Format_RGB32);
        cover.fill(QColor(40, 90, 160));
        const QString coverPath = steam.root + QStringLiteral("/appcache/librarycache/1001/library_600x900.jpg");
        QVERIFY(QDir().mkpath(QFileInfo(coverPath).path()));
        QVERIFY(cover.save(coverPath, "JPEG"));
        QTemporaryDir config;
        QTemporaryDir proc;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        GameLibrary library(steam.options(), &store, nullptr, proc.path());
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{true});

        Window w(library, editor);
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

        Window w(library, editor);
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

    void showsWhenSteamIsMissing()
    {
        QTemporaryDir home;
        QTemporaryDir config;
        settings::SettingsStore store(config.filePath(QStringLiteral("gfxtune")));
        steam::ScanOptions options;
        options.homePath = home.path();
        GameLibrary library(options, &store);
        GameSettingsEditor editor(&library, &store, settings::SystemInfo{});

        Window w(library, editor);
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
