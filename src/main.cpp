// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    // KDE's desktop style draws through QStyle, which needs a QApplication.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gfxtune"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    // KDE's desktop style, unless the user asked for another one.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Gfxtune", "Main");

    return QApplication::exec();
}
