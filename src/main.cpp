// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Commands.h"
#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "GpuMonitor.h"
#include "performance/Performance.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/QQmlExtensionPlugin>

#include <cstring>

Q_IMPORT_QML_PLUGIN(Gfxtune_BackendPlugin)
Q_IMPORT_QML_PLUGIN(GfxtunePlugin)

namespace
{

// Options that print something and exit; they must work without a display.
bool wantsTextOnly(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        for (const char *option : {"--list-games", "--remove-launch-lines", "--help", "--help-all", "-h", "-?", "--version", "-v"}) {
            if (std::strcmp(argv[i], option) == 0) {
                return true;
            }
        }
    }
    return false;
}

void setUpCommandLine(QCommandLineParser &parser)
{
    parser.setApplicationDescription(i18n("Graphics and game tuning for Steam games on CachyOS"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("list-games"), i18n("Print the Steam games gfxtune finds, then exit.")});
    parser.addOption({QStringLiteral("remove-launch-lines"),
                      i18n("Take gfxtune-run out of every game's Steam launch options (Steam must be closed), then exit.")});
}

} // namespace

int main(int argc, char *argv[])
{
    // Before any text is made: the catalog that i18n() reads.
    KLocalizedString::setApplicationDomain("gfxtune");
    if (wantsTextOnly(argc, argv)) {
        const QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("gfxtune"));
        QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
        QCommandLineParser parser;
        setUpCommandLine(parser);
        parser.process(app); // handles --help and --version itself
        QTextStream out(stdout);
        if (parser.isSet(QStringLiteral("remove-launch-lines"))) {
            return gfxtune::removeLaunchLines(out);
        }
        return gfxtune::listGames(out);
    }

    // KDE's desktop style draws through QStyle, which needs a QApplication.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gfxtune"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("gfxtune"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("gfxtune"), QIcon(QStringLiteral(":/gfxtune.svg"))));
    QCommandLineParser parser;
    setUpCommandLine(parser);
    parser.process(app);

    // KDE's desktop style, unless the user asked for another one.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // Declared in this order so the editor, which saves on destruction, goes
    // before the library it reads from.
    gfxtune::settings::SettingsStore store(gfxtune::settings::SettingsStore::defaultBaseDir());
    gfxtune::GameLibrary library(gfxtune::steam::ScanOptions::fromEnvironment(), &store);
    gfxtune::GameSettingsEditor editor(&library, &store, gfxtune::settings::SystemInfo::detect());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &editor, &gfxtune::GameSettingsEditor::flush);
    // GFXTUNE_SYSFS_ROOT points the card panel at a saved copy of /sys, for
    // testing and for reproducing reports.
    gfxtune::GpuMonitor gpu(qEnvironmentVariable("GFXTUNE_SYSFS_ROOT", QStringLiteral("/sys")));
    gpu.setGameSources(gfxtune::performance::performanceRoot(store.baseDir()));

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.setInitialProperties({
        {QStringLiteral("library"), QVariant::fromValue(&library)},
        {QStringLiteral("editor"), QVariant::fromValue(&editor)},
        {QStringLiteral("gpu"), QVariant::fromValue(&gpu)},
    });
    engine.loadFromModule("Gfxtune", "Main");
    library.start();

    return QApplication::exec();
}
