// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameLibrary.h"
#include "GameSettingsEditor.h"
#include "ListGames.h"

#include <QApplication>
#include <QCommandLineParser>
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
        for (const char *option : {"--list-games", "--help", "--help-all", "-h", "-?", "--version", "-v"}) {
            if (std::strcmp(argv[i], option) == 0) {
                return true;
            }
        }
    }
    return false;
}

void setUpCommandLine(QCommandLineParser &parser)
{
    parser.setApplicationDescription(QStringLiteral("Per-game graphics settings and live GPU readings"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("list-games"), QStringLiteral("Print the Steam games gfxtune finds, then exit.")});
}

} // namespace

int main(int argc, char *argv[])
{
    if (wantsTextOnly(argc, argv)) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("gfxtune"));
        QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
        QCommandLineParser parser;
        setUpCommandLine(parser);
        parser.process(app); // handles --help and --version itself
        QTextStream out(stdout);
        return gfxtune::listGames(out);
    }

    // KDE's desktop style draws through QStyle, which needs a QApplication.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gfxtune"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("gfxtune"));
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

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.setInitialProperties({
        {QStringLiteral("library"), QVariant::fromValue(&library)},
        {QStringLiteral("editor"), QVariant::fromValue(&editor)},
    });
    engine.loadFromModule("Gfxtune", "Main");
    library.start();

    return QApplication::exec();
}
