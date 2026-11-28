// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "LsfgVk.h"

#include "shared/Sizes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace gfxtune::framegen
{

namespace
{

QString environmentOr(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

// A TOML basic string.
QString tomlString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\')) {
            out += QLatin1Char('\\') + c;
        } else if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            out += QStringLiteral("\\u%1").arg(int(c.unicode()), 4, 16, QLatin1Char('0'));
        } else {
            out += c;
        }
    }
    return out + QLatin1Char('"');
}

bool namesLayer(const QJsonObject &layer)
{
    return layer.value(QLatin1String("name")).toString() == QLatin1String(LayerName);
}

} // namespace

QStringList implicitLayerDirs()
{
    // The loader's order (docs/LoaderLayerInterface.md in Vulkan-Loader):
    // the user's and the system's configuration folders, then data folders.
    const QString home = QDir::homePath();
    QStringList roots{environmentOr("XDG_CONFIG_HOME", home + QStringLiteral("/.config"))};
    roots += environmentOr("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    roots << QStringLiteral("/etc") << environmentOr("XDG_DATA_HOME", home + QStringLiteral("/.local/share"));
    roots += environmentOr("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QStringList dirs;
    for (const QString &root : std::as_const(roots)) {
        const QString dir = root + QStringLiteral("/vulkan/implicit_layer.d");
        if (!dirs.contains(dir)) {
            dirs.append(dir);
        }
    }
    return dirs;
}

bool layerInstalled(const QStringList &dirs)
{
    for (const QString &dir : dirs) {
        const QStringList manifests = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
        for (const QString &name : manifests) {
            QFile file(dir + QLatin1Char('/') + name);
            // A manifest is a few hundred bytes; anything far bigger is not one.
            if (!file.open(QIODevice::ReadOnly) || file.size() > MiB) {
                continue;
            }
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            if (namesLayer(root.value(QLatin1String("layer")).toObject())) {
                return true;
            }
            const QJsonArray layers = root.value(QLatin1String("layers")).toArray();
            for (const auto &layer : layers) {
                if (namesLayer(layer.toObject())) {
                    return true;
                }
            }
        }
    }
    return false;
}

QString configPath(const QString &baseDir, quint32 appId)
{
    return baseDir + QStringLiteral("/lsfg-vk/%1/conf.toml").arg(appId);
}

QByteArray configText(const settings::GameSettings &s, const QString &dll)
{
    QString text = QStringLiteral(
        "# lsfg-vk's settings for one game, written by gfxtune from its own\n"
        "# settings: changes made here are overwritten.\n"
        "version = 2\n");
    if (!dll.isEmpty()) {
        text += QStringLiteral("\n[global]\ndll = %1\n").arg(tomlString(dll));
    }
    text += QStringLiteral("\n[[profile]]\nname = \"%1\"\nactive_in = []\n").arg(QLatin1String(ProfileName));
    text += QStringLiteral("multiplier = %1\n").arg(qMax(2, s.frameGeneration));
    text += QStringLiteral("flow_scale = %1\n").arg(QString::number(s.flowScale / 100.0, 'f', 2));
    text += QStringLiteral("performance_mode = %1\n").arg(s.frameGenerationPerformance ? QStringLiteral("true") : QStringLiteral("false"));
    return text.toUtf8();
}

bool writeConfig(const QString &path, const QByteArray &text, QString *error)
{
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.size() <= MiB && current.readAll() == text) {
        return true;
    }
    current.close();
    if (!QDir().mkpath(QFileInfo(path).path())) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(QFileInfo(path).path());
        }
        return false;
    }
    // Written in one piece: lsfg-vk notices the finished file moved in.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QString dllNotePath(const QString &baseDir)
{
    return baseDir + QStringLiteral("/lsfg-vk/lossless-scaling");
}

QString readDllNote(const QString &baseDir)
{
    QFile file(dllNotePath(baseDir));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * KiB) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool writeDllNote(const QString &baseDir, const QString &dll, QString *error)
{
    const QString path = dllNotePath(baseDir);
    if (dll.isEmpty()) {
        return !QFile::exists(path) || QFile::remove(path);
    }
    return writeConfig(path, dll.toUtf8() + '\n', error);
}

} // namespace gfxtune::framegen
