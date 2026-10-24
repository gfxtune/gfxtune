// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "SettingsStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace gfxtune::settings
{

namespace
{

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

class Reader
{
public:
    Reader(const QString &path, QStringList *warnings)
        : m_file(path, QSettings::IniFormat)
        , m_warnings(warnings)
    {
        if (m_file.status() != QSettings::NoError) {
            warn(QStringLiteral("%1 could not be read").arg(path));
        }
    }

    bool flag(const QString &key, bool fallback)
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        const QString text = m_file.value(key).toString().trimmed().toLower();
        if (text == QLatin1String("true")) {
            return true;
        }
        if (text == QLatin1String("false")) {
            return false;
        }
        warn(QStringLiteral("%1: \"%2\" is not true or false").arg(key, text));
        return fallback;
    }

    int number(const QString &key, int fallback, int min, int max)
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        bool ok = false;
        const int value = m_file.value(key).toString().trimmed().toInt(&ok);
        if (!ok || value < min || value > max) {
            warn(QStringLiteral("%1: expected a whole number from %2 to %3").arg(key).arg(min).arg(max));
            return fallback;
        }
        return value;
    }

    int oneOf(const QString &key, int fallback, const QList<int> &allowed)
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        bool ok = false;
        const int value = m_file.value(key).toString().trimmed().toInt(&ok);
        if (!ok || !allowed.contains(value)) {
            warn(QStringLiteral("%1: not one of the offered values").arg(key));
            return fallback;
        }
        return value;
    }

    template<typename Enum>
    Enum choice(const QString &key, Enum fallback)
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        Enum value = fallback;
        if (!fromKey(m_file.value(key).toString(), value)) {
            warn(QStringLiteral("%1: unknown value").arg(key));
            return fallback;
        }
        return value;
    }

private:
    void warn(const QString &message)
    {
        if (m_warnings) {
            m_warnings->append(message);
        }
    }

    QSettings m_file;
    QStringList *m_warnings;
};

} // namespace

SettingsStore::SettingsStore(QString baseDir)
    : m_baseDir(QDir::cleanPath(std::move(baseDir)))
{
}

QString SettingsStore::defaultBaseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/gfxtune");
}

QString SettingsStore::pathFor(quint32 appId) const
{
    return m_baseDir + QStringLiteral("/games/%1.ini").arg(appId);
}

GameSettings SettingsStore::load(quint32 appId, QStringList *warnings) const
{
    const GameSettings defaults;
    GameSettings s;
    const QString path = pathFor(appId);
    if (!QFileInfo::exists(path)) {
        return s;
    }
    Reader in(path, warnings);
    s.anisotropicFiltering = in.oneOf(QStringLiteral("image/anisotropic_filtering"), defaults.anisotropicFiltering,
                                      GameSettings::AnisotropicLevels);
    s.frameCapEnabled = in.flag(QStringLiteral("frames/frame_cap_enabled"), defaults.frameCapEnabled);
    s.frameCap = in.number(QStringLiteral("frames/frame_cap"), defaults.frameCap, GameSettings::MinFrameCap, GameSettings::MaxFrameCap);
    s.verticalSync = in.choice(QStringLiteral("frames/vertical_sync"), defaults.verticalSync);
    // 0.1.2 and older wrote an on/off switch; on meant what On means now.
    const bool oldUpgrade = in.flag(QStringLiteral("upscaling/fsr4_upgrade"), false);
    s.fsr4 = in.choice(QStringLiteral("upscaling/fsr4"), oldUpgrade ? Fsr4::On : defaults.fsr4);
    s.gamescope = in.flag(QStringLiteral("gamescope/enabled"), defaults.gamescope);
    s.upscaleFilter = in.choice(QStringLiteral("gamescope/filter"), defaults.upscaleFilter);
    s.scalingMode = in.choice(QStringLiteral("gamescope/scaling"), defaults.scalingMode);
    s.renderScale = in.oneOf(QStringLiteral("gamescope/render_scale"), defaults.renderScale, GameSettings::RenderScales);
    s.sharpness = in.number(QStringLiteral("gamescope/sharpness"), defaults.sharpness, 0, GameSettings::MaxSharpness);
    return s;
}

bool SettingsStore::save(quint32 appId, const GameSettings &s, const QString &gameName, QString *error) const
{
    if (s.isDefault()) {
        return remove(appId, error);
    }
    const QString path = pathFor(appId);
    if (!QDir().mkpath(QFileInfo(path).path())) {
        return fail(error, QStringLiteral("could not create %1").arg(QFileInfo(path).path()));
    }

    QSettings out(path, QSettings::IniFormat);
    out.clear();
    out.setValue(QStringLiteral("game/name"), gameName);
    out.setValue(QStringLiteral("game/steam_app_id"), appId);
    out.setValue(QStringLiteral("image/anisotropic_filtering"), s.anisotropicFiltering);
    out.setValue(QStringLiteral("frames/frame_cap_enabled"), s.frameCapEnabled);
    out.setValue(QStringLiteral("frames/frame_cap"), s.frameCap);
    out.setValue(QStringLiteral("frames/vertical_sync"), toKey(s.verticalSync));
    out.setValue(QStringLiteral("upscaling/fsr4"), toKey(s.fsr4));
    out.setValue(QStringLiteral("gamescope/enabled"), s.gamescope);
    out.setValue(QStringLiteral("gamescope/filter"), toKey(s.upscaleFilter));
    out.setValue(QStringLiteral("gamescope/scaling"), toKey(s.scalingMode));
    out.setValue(QStringLiteral("gamescope/render_scale"), s.renderScale);
    out.setValue(QStringLiteral("gamescope/sharpness"), s.sharpness);
    out.sync(); // QSettings writes through a temporary file and renames it
    if (out.status() != QSettings::NoError) {
        return fail(error, QStringLiteral("could not write %1").arg(path));
    }
    return true;
}

bool SettingsStore::remove(quint32 appId, QString *error) const
{
    QFile file(pathFor(appId));
    if (file.exists() && !file.remove()) {
        return fail(error, file.errorString());
    }
    return true;
}

QSet<quint32> SettingsStore::customizedGames() const
{
    QSet<quint32> ids;
    const QStringList files = QDir(m_baseDir + QStringLiteral("/games")).entryList({QStringLiteral("*.ini")}, QDir::Files);
    for (const QString &name : files) {
        bool ok = false;
        const quint32 id = QFileInfo(name).completeBaseName().toUInt(&ok);
        if (ok && id != 0) {
            ids.insert(id);
        }
    }
    return ids;
}

bool SettingsStore::removeAll(QString *error) const
{
    // Guard against ever deleting anything but our own folder.
    if (QFileInfo(m_baseDir).fileName() != QLatin1String("gfxtune")) {
        return fail(error, QStringLiteral("refusing to delete %1: not a gfxtune folder").arg(m_baseDir));
    }
    QDir dir(m_baseDir);
    if (!dir.exists()) {
        return true;
    }
    if (!dir.removeRecursively()) {
        return fail(error, QStringLiteral("could not delete everything in %1").arg(m_baseDir));
    }
    return true;
}

} // namespace gfxtune::settings
