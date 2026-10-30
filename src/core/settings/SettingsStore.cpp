// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "SettingsStore.h"

#include "Profile.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>
#include <utility>

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

    bool has(const QString &key) const { return m_file.contains(key); }
    // The file holds a usable value for the key.
    bool accepted(const QString &key) const { return has(key) && !m_rejected.contains(key); }

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
        reject(key, QStringLiteral("%1: \"%2\" is not true or false").arg(key, text));
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
            reject(key, QStringLiteral("%1: expected a whole number from %2 to %3").arg(key).arg(min).arg(max));
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
            reject(key, QStringLiteral("%1: not one of the offered values").arg(key));
            return fallback;
        }
        return value;
    }

    // One of a list of names, as its index.
    template<std::size_t N>
    int named(const QString &key, int fallback, const char *const (&names)[N])
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        const QString text = m_file.value(key).toString().trimmed().toLower();
        for (std::size_t i = 0; i < N; ++i) {
            if (text == QLatin1String(names[i])) {
                return int(i);
            }
        }
        reject(key, QStringLiteral("%1: unknown value").arg(key));
        return fallback;
    }

    template<typename Enum>
    Enum choice(const QString &key, Enum fallback)
    {
        if (!m_file.contains(key)) {
            return fallback;
        }
        Enum value = fallback;
        if (!fromKey(m_file.value(key).toString(), value)) {
            reject(key, QStringLiteral("%1: unknown value").arg(key));
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
    void reject(const QString &key, const QString &message)
    {
        m_rejected.insert(key);
        warn(message);
    }

    QSettings m_file;
    QStringList *m_warnings;
    QSet<QString> m_rejected;
};

// Reads every setting the file has onto `s`; what it lacks stays as it was.
void readSettings(Reader &in, GameSettings &s)
{
    s.anisotropicFiltering = in.oneOf(QStringLiteral("image/anisotropic_filtering"), s.anisotropicFiltering, GameSettings::AnisotropicLevels);
    s.lodBias = in.oneOf(QStringLiteral("image/lod_bias"), s.lodBias, GameSettings::LodBiasLevels);
    s.frameCapEnabled = in.flag(QStringLiteral("frames/frame_cap_enabled"), s.frameCapEnabled);
    s.frameCap = in.number(QStringLiteral("frames/frame_cap"), s.frameCap, GameSettings::MinFrameCap, GameSettings::MaxFrameCap);
    s.verticalSync = in.choice(QStringLiteral("frames/vertical_sync"), s.verticalSync);
    // 0.1.2 and older wrote an on/off switch; on meant what On means now.
    const bool oldUpgrade = in.flag(QStringLiteral("upscaling/fsr4_upgrade"), false);
    s.fsr4 = in.choice(QStringLiteral("upscaling/fsr4"), oldUpgrade ? Fsr4::On : s.fsr4);
    s.gamescope = in.flag(QStringLiteral("gamescope/enabled"), s.gamescope);
    s.upscaleFilter = in.choice(QStringLiteral("gamescope/filter"), s.upscaleFilter);
    s.scalingMode = in.choice(QStringLiteral("gamescope/scaling"), s.scalingMode);
    s.renderScale = in.oneOf(QStringLiteral("gamescope/render_scale"), s.renderScale, GameSettings::RenderScales);
    s.sharpness = in.number(QStringLiteral("gamescope/sharpness"), s.sharpness, 0, GameSettings::MaxSharpness);

    const auto ranged = [&in](const char *key, int current, const filters::Range &range) {
        return in.number(QStringLiteral("filters/") + QLatin1String(key), current, range.min, range.max);
    };
    filters::Values &f = s.filters;
    f.enabled = in.flag(QStringLiteral("filters/enabled"), f.enabled);
    f.sharpening = ranged("sharpening", f.sharpening, filters::Sharpening);
    f.brightness = ranged("brightness", f.brightness, filters::Brightness);
    f.contrast = ranged("contrast", f.contrast, filters::Contrast);
    f.gamma = ranged("gamma", f.gamma, filters::Gamma);
    f.saturation = ranged("saturation", f.saturation, filters::Saturation);
    f.vibrance = ranged("vibrance", f.vibrance, filters::Vibrance);
    f.temperature = ranged("temperature", f.temperature, filters::Temperature);
    f.antiAliasing = in.named(QStringLiteral("filters/anti_aliasing"), f.antiAliasing, filters::AntiAliasingNames);
    s.measurePerformance = in.flag(QStringLiteral("performance/measure"), s.measurePerformance);
    s.gameMode = in.flag(QStringLiteral("launch/gamemode"), s.gameMode);
    s.mangoHud = in.flag(QStringLiteral("launch/mangohud"), s.mangoHud);
}

// Every setting as it is written. The filters, frame cap and texture detail
// are also read by the Vulkan layer while the game runs, without Qt: keep
// them plain "key=number" lines (see src/layer/LayerSettings.cpp).
QList<std::pair<QString, QVariant>> keyValues(const GameSettings &s)
{
    const filters::Values &f = s.filters;
    return {
        {QStringLiteral("image/anisotropic_filtering"), s.anisotropicFiltering},
        {QStringLiteral("image/lod_bias"), s.lodBias},
        {QStringLiteral("frames/frame_cap_enabled"), s.frameCapEnabled},
        {QStringLiteral("frames/frame_cap"), s.frameCap},
        {QStringLiteral("frames/vertical_sync"), toKey(s.verticalSync)},
        {QStringLiteral("upscaling/fsr4"), toKey(s.fsr4)},
        {QStringLiteral("gamescope/enabled"), s.gamescope},
        {QStringLiteral("gamescope/filter"), toKey(s.upscaleFilter)},
        {QStringLiteral("gamescope/scaling"), toKey(s.scalingMode)},
        {QStringLiteral("gamescope/render_scale"), s.renderScale},
        {QStringLiteral("gamescope/sharpness"), s.sharpness},
        {QStringLiteral("filters/enabled"), f.enabled},
        {QStringLiteral("filters/sharpening"), f.sharpening},
        {QStringLiteral("filters/brightness"), f.brightness},
        {QStringLiteral("filters/contrast"), f.contrast},
        {QStringLiteral("filters/gamma"), f.gamma},
        {QStringLiteral("filters/saturation"), f.saturation},
        {QStringLiteral("filters/vibrance"), f.vibrance},
        {QStringLiteral("filters/temperature"), f.temperature},
        {QStringLiteral("filters/anti_aliasing"), QString::fromLatin1(filters::AntiAliasingNames[filters::AntiAliasing.clamp(f.antiAliasing)])},
        {QStringLiteral("performance/measure"), s.measurePerformance},
        {QStringLiteral("launch/gamemode"), s.gameMode},
        {QStringLiteral("launch/mangohud"), s.mangoHud},
    };
}

// Game files written since global settings exist hold only the game's own
// changes; older ones hold every setting.
constexpr int FileFormat = 2;

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

QString SettingsStore::globalPath() const
{
    return m_baseDir + QStringLiteral("/global.ini");
}

GameSettings SettingsStore::loadGlobal(QStringList *warnings) const
{
    GameSettings s;
    if (QFileInfo::exists(globalPath())) {
        Reader in(globalPath(), warnings);
        readSettings(in, s);
    }
    return s;
}

bool SettingsStore::saveGlobal(const GameSettings &settings, QString *error) const
{
    GameSettings s = settings;
    s.filtersRiskAccepted = false; // only ever a game's own
    const QString path = globalPath();
    if (s.isDefault()) {
        QFile file(path);
        return !file.exists() || file.remove() || fail(error, file.errorString());
    }
    if (!QDir().mkpath(m_baseDir)) {
        return fail(error, QStringLiteral("could not create %1").arg(m_baseDir));
    }
    QSettings out(path, QSettings::IniFormat);
    out.clear();
    for (const auto &[key, value] : keyValues(s)) {
        out.setValue(key, value);
    }
    out.sync(); // QSettings writes through a temporary file and renames it
    return out.status() == QSettings::NoError || fail(error, QStringLiteral("could not write %1").arg(path));
}

GameProfile SettingsStore::loadProfile(quint32 appId, QStringList *warnings) const
{
    GameProfile profile;
    const QString path = pathFor(appId);
    if (!QFileInfo::exists(path)) {
        return profile;
    }
    Reader in(path, warnings);
    readSettings(in, profile.own);
    profile.own.filtersRiskAccepted = in.flag(QStringLiteral("filters/risk_accepted"), false);
    profile.usesGlobal = in.flag(QStringLiteral("game/uses_global"), true);
    if (in.has(QStringLiteral("game/format"))) {
        for (const SettingKind &kind : settingKinds()) {
            // A damaged value falls back to the global setting.
            if (std::any_of(kind.keys.cbegin(), kind.keys.cend(), [&in](const QString &key) { return in.accepted(key); })) {
                profile.changed.insert(kind.id);
            }
        }
    } else {
        // Written before global settings existed, with every setting: those
        // left at gfxtune's defaults were not chosen for the game.
        profile.changed = profileFrom(profile.own).changed;
    }
    return profile;
}

bool SettingsStore::saveProfile(quint32 appId, const GameProfile &profile, const QString &gameName, QString *error) const
{
    return write(appId, profile, loadFacts(appId), gameName, error);
}

GameSettings SettingsStore::load(quint32 appId, QStringList *warnings) const
{
    return effectiveSettings(loadGlobal(warnings), loadProfile(appId, warnings));
}

bool SettingsStore::write(quint32 appId, const GameProfile &profile, const GameFacts &facts, const QString &gameName, QString *error) const
{
    if (profile.isEmpty() && facts == GameFacts{}) {
        return remove(appId, error);
    }
    const QString path = pathFor(appId);
    if (!QDir().mkpath(QFileInfo(path).path())) {
        return fail(error, QStringLiteral("could not create %1").arg(QFileInfo(path).path()));
    }

    QSettings out(path, QSettings::IniFormat);
    const QString name = gameName.isEmpty() ? out.value(QStringLiteral("game/name")).toString() : gameName;
    out.clear();
    if (!name.isEmpty()) {
        out.setValue(QStringLiteral("game/name"), name);
    }
    out.setValue(QStringLiteral("game/steam_app_id"), appId);
    out.setValue(QStringLiteral("game/format"), FileFormat);
    if (!profile.usesGlobal) {
        out.setValue(QStringLiteral("game/uses_global"), false);
    }
    if (!facts.antiCheat.isEmpty()) {
        out.setValue(QStringLiteral("game/anti_cheat"), facts.antiCheat);
    }
    if (facts.openGlOnly) {
        out.setValue(QStringLiteral("game/opengl_only"), true);
    }
    QSet<QString> keys;
    for (const SettingKind &kind : settingKinds()) {
        if (profile.changed.contains(kind.id)) {
            keys.unite(QSet<QString>(kind.keys.cbegin(), kind.keys.cend()));
        }
    }
    for (const auto &[key, value] : keyValues(profile.own)) {
        if (keys.contains(key)) {
            out.setValue(key, value);
        }
    }
    if (profile.own.filtersRiskAccepted) {
        out.setValue(QStringLiteral("filters/risk_accepted"), true);
    }
    out.sync(); // QSettings writes through a temporary file and renames it
    if (out.status() != QSettings::NoError) {
        return fail(error, QStringLiteral("could not write %1").arg(path));
    }
    return true;
}

GameFacts SettingsStore::loadFacts(quint32 appId) const
{
    GameFacts facts;
    const QString path = pathFor(appId);
    if (!QFileInfo::exists(path)) {
        return facts;
    }
    const QSettings in(path, QSettings::IniFormat);
    for (const QString &name : in.value(QStringLiteral("game/anti_cheat")).toStringList()) {
        if (!name.trimmed().isEmpty()) {
            facts.antiCheat.append(name.trimmed());
        }
    }
    facts.openGlOnly = in.value(QStringLiteral("game/opengl_only")).toString().trimmed().toLower() == QLatin1String("true");
    return facts;
}

bool SettingsStore::saveFacts(quint32 appId, const GameFacts &facts, const QString &gameName, QString *error) const
{
    if (loadFacts(appId) == facts) {
        return true; // only when they changed
    }
    return write(appId, loadProfile(appId), facts, gameName, error);
}

bool SettingsStore::remove(quint32 appId, QString *error) const
{
    QFile file(pathFor(appId));
    if (file.exists() && !file.remove()) {
        return fail(error, file.errorString());
    }
    return true;
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
