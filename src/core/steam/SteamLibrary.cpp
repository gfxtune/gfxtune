// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "SteamLibrary.h"

#include "AppInfo.h"
#include "Covers.h"
#include "GameFiles.h"
#include "vdf/TextVdf.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>

namespace gfxtune::steam
{

namespace
{

// The app whose cache entry holds Valve's Steam Play tool list and per-game
// tool assignments.
constexpr quint32 SteamPlayManifestAppId = 891390;
// Steamworks Common Redistributables: installed like a game, but is not one.
constexpr quint32 SteamworksRedistAppId = 228980;
// appmanifest StateFlags bit set once every file of the app is on disk.
constexpr quint32 StateFullyInstalled = 4;

struct Manifest {
    quint32 appId = 0;
    QString name;
    QString buildId;
    QString libraryPath;
    QString installPath;
};

struct ToolInfo {
    QString displayName;
    QString installPath;
    QString fromOs;
    QString toOs;
    bool known = false;

    bool translatesWindows() const { return fromOs.contains(QLatin1String("windows")) && toOs.contains(QLatin1String("linux")); }
};

QString canonicalDir(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() ? info.canonicalFilePath() : QString();
}

QString findSteamRoot(const QString &home)
{
    const QStringList candidates{
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.local/share/Steam"),
    };
    for (const QString &candidate : candidates) {
        const QString root = canonicalDir(candidate);
        if (!root.isEmpty() && QFileInfo(root + QStringLiteral("/steamapps")).isDir()) {
            return root;
        }
    }
    return {};
}

bool readVdf(const QString &path, vdf::Node &root, QStringList &warnings)
{
    if (!QFileInfo::exists(path)) {
        return false;
    }
    QString problem;
    if (!vdf::readTextFile(path, root, &problem)) {
        warnings.append(QStringLiteral("%1: %2").arg(path, problem));
        return false;
    }
    return true;
}

QList<LibraryFolder> readLibraries(const QString &steamRoot, QStringList &warnings)
{
    QList<LibraryFolder> libraries{{steamRoot, true}};
    QSet<QString> seen{steamRoot};

    vdf::Node file;
    if (!readVdf(steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"), file, warnings)) {
        return libraries;
    }
    const vdf::Node *list = file.child(u"libraryfolders");
    if (!list) {
        return libraries;
    }
    for (const vdf::Node &entry : list->children) {
        bool numbered = false;
        entry.key.toUInt(&numbered);
        if (!numbered) {
            continue; // other keys such as "contentstatsid"
        }
        // Current Steam writes a section with a "path"; old versions wrote
        // the path as the value itself.
        const QString path = entry.isSection ? entry.valueOf(u"path") : entry.value;
        if (path.isEmpty()) {
            continue;
        }
        const QString canonical = canonicalDir(path);
        const QString key = canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        libraries.append({key, !canonical.isEmpty() && QFileInfo(canonical + QStringLiteral("/steamapps")).isDir()});
    }
    return libraries;
}

// Installed apps of one library, split into games-or-unknown and Steam's
// own tools (Proton, the runtimes), recognized by their toolmanifest.vdf.
void readManifests(const QString &library, std::vector<Manifest> &apps, QHash<quint32, Manifest> &tools, QList<SkippedApp> &skipped,
                   QStringList &warnings)
{
    const QDir steamapps(library + QStringLiteral("/steamapps"));
    const QStringList files = steamapps.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        vdf::Node file;
        if (!readVdf(steamapps.filePath(fileName), file, warnings)) {
            continue;
        }
        const vdf::Node *state = file.child(u"AppState");
        if (!state) {
            warnings.append(QStringLiteral("%1: no AppState section").arg(steamapps.filePath(fileName)));
            continue;
        }
        Manifest app;
        app.appId = state->valueOf(u"appid").toUInt();
        app.name = state->valueOf(u"name");
        app.buildId = state->valueOf(u"buildid");
        if (app.name.isEmpty()) {
            if (const vdf::Node *userConfig = state->child(u"UserConfig")) {
                app.name = userConfig->valueOf(u"name");
            }
        }
        const QString installDir = state->valueOf(u"installdir");
        if (app.appId == 0 || installDir.isEmpty()) {
            warnings.append(QStringLiteral("%1: app id or install folder missing").arg(steamapps.filePath(fileName)));
            continue;
        }
        const QString flags = state->valueOf(u"StateFlags");
        if (!flags.isEmpty() && (flags.toUInt() & StateFullyInstalled) == 0) {
            skipped.append({app.appId, app.name, QStringLiteral("not fully installed (Steam state %1)").arg(flags)});
            continue;
        }
        app.libraryPath = library;
        app.installPath = QDir::cleanPath(steamapps.filePath(QStringLiteral("common/") + installDir));
        if (!QFileInfo(app.installPath).isDir()) {
            skipped.append({app.appId, app.name, QStringLiteral("its folder is missing: %1").arg(app.installPath)});
            continue;
        }
        if (QFileInfo::exists(app.installPath + QStringLiteral("/toolmanifest.vdf"))) {
            tools.insert(app.appId, app);
            skipped.append({app.appId, app.name, QStringLiteral("a Steam tool (Proton or a runtime), not a game")});
        } else {
            apps.push_back(app);
        }
    }
}

// The account last signed in: the one marked MostRecent, otherwise the one
// with the newest sign-in time.
quint32 readAccountId(const QString &steamRoot, QStringList &warnings)
{
    vdf::Node file;
    if (!readVdf(steamRoot + QStringLiteral("/config/loginusers.vdf"), file, warnings)) {
        return 0;
    }
    const vdf::Node *users = file.child(u"users");
    if (!users) {
        return 0;
    }
    quint64 chosen = 0;
    qint64 newest = -1;
    for (const vdf::Node &user : users->children) {
        bool ok = false;
        const quint64 steamId = user.key.toULongLong(&ok);
        if (!ok || !user.isSection) {
            continue;
        }
        if (user.valueOf(u"MostRecent") == QLatin1String("1")) {
            chosen = steamId;
            break;
        }
        const qint64 timestamp = user.valueOf(u"Timestamp").toLongLong();
        if (timestamp > newest) {
            newest = timestamp;
            chosen = steamId;
        }
    }
    // The folder under userdata/ is the low 32 bits of the 64-bit Steam ID.
    return quint32(chosen & 0xFFFFFFFFu);
}

QHash<quint32, QString> readLaunchOptions(const QString &localConfigPath, QStringList &warnings)
{
    QHash<quint32, QString> options;
    vdf::Node file;
    if (!readVdf(localConfigPath, file, warnings)) {
        return options;
    }
    const vdf::Node *apps = file.find({u"UserLocalConfigStore", u"Software", u"Valve", u"Steam", u"apps"});
    if (!apps) {
        return options;
    }
    for (const vdf::Node &app : apps->children) {
        const QString value = app.valueOf(u"LaunchOptions");
        if (!value.isEmpty()) {
            options.insert(app.key.toUInt(), value);
        }
    }
    return options;
}

// CompatToolMapping from config.vdf: app id -> tool chosen in Steam's
// Properties > Compatibility. Key "0" is the default for all other titles.
QHash<QString, QString> readCompatMapping(const QString &steamRoot, QStringList &warnings)
{
    QHash<QString, QString> mapping;
    vdf::Node file;
    if (!readVdf(steamRoot + QStringLiteral("/config/config.vdf"), file, warnings)) {
        return mapping;
    }
    const vdf::Node *section = file.find({u"InstallConfigStore", u"Software", u"Valve", u"Steam", u"CompatToolMapping"});
    if (!section) {
        return mapping;
    }
    for (const vdf::Node &entry : section->children) {
        const QString name = entry.valueOf(u"name");
        if (!name.isEmpty()) {
            mapping.insert(entry.key, name);
        }
    }
    return mapping;
}

// Tools installed outside Steam's own downloads, each described by a
// compatibilitytool.vdf. Later folders win, as in Steam.
QHash<QString, ToolInfo> readCustomTools(const QStringList &folders, QStringList &warnings)
{
    QHash<QString, ToolInfo> tools;
    for (const QString &folder : folders) {
        const QDir dir(folder);
        if (!dir.exists()) {
            continue;
        }
        QStringList manifests;
        for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString path = dir.filePath(sub + QStringLiteral("/compatibilitytool.vdf"));
            if (QFileInfo::exists(path)) {
                manifests.append(path);
            }
        }
        for (const QString &file : dir.entryList({QStringLiteral("*.vdf")}, QDir::Files, QDir::Name)) {
            manifests.append(dir.filePath(file));
        }

        for (const QString &path : std::as_const(manifests)) {
            vdf::Node file;
            if (!readVdf(path, file, warnings)) {
                continue;
            }
            const vdf::Node *list = file.find({u"compatibilitytools", u"compat_tools"});
            if (!list) {
                continue;
            }
            for (const vdf::Node &tool : list->children) {
                ToolInfo info;
                info.known = true;
                info.displayName = tool.valueOf(u"display_name");
                if (info.displayName.isEmpty()) {
                    info.displayName = tool.key;
                }
                info.fromOs = tool.valueOf(u"from_oslist");
                info.toOs = tool.valueOf(u"to_oslist");
                const QString installPath = tool.valueOf(u"install_path");
                const QString base = QFileInfo(path).path();
                if (installPath == QLatin1String(".") || installPath.isEmpty()) {
                    info.installPath = base;
                } else if (QDir::isAbsolutePath(installPath)) {
                    info.installPath = QDir::cleanPath(installPath);
                } else {
                    info.installPath = QDir::cleanPath(folder + QLatin1Char('/') + installPath);
                }
                tools.insert(tool.key, info);
            }
        }
    }
    return tools;
}

class ToolCatalog
{
public:
    QHash<QString, ToolInfo> custom;
    const vdf::Node *valveTools = nullptr; // compat_tools of the Steam Play manifest
    QHash<quint32, Manifest> installedToolApps;

    ToolInfo lookup(const QString &name) const
    {
        if (const auto it = custom.constFind(name); it != custom.constEnd()) {
            return *it;
        }
        if (const vdf::Node *entry = valveEntry(name)) {
            ToolInfo info;
            info.known = true;
            info.fromOs = entry->valueOf(u"from_oslist");
            info.toOs = entry->valueOf(u"to_oslist");
            info.displayName = entry->valueOf(u"display_name");
            const auto app = installedToolApps.constFind(entry->valueOf(u"appid").toUInt());
            if (app != installedToolApps.constEnd()) {
                info.installPath = app->installPath;
                if (!app->name.isEmpty()) {
                    info.displayName = app->name;
                }
            }
            if (info.displayName.isEmpty()) {
                info.displayName = name;
            }
            return info;
        }
        ToolInfo unknown;
        unknown.displayName = name;
        return unknown;
    }

private:
    // A tool can be known under several names: its own and its aliases.
    const vdf::Node *valveEntry(const QString &name) const
    {
        if (!valveTools) {
            return nullptr;
        }
        for (const vdf::Node &entry : valveTools->children) {
            if (entry.key == name) {
                return &entry;
            }
        }
        for (const vdf::Node &entry : valveTools->children) {
            const QStringList aliases = entry.valueOf(u"aliases").split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (aliases.contains(name)) {
                return &entry;
            }
        }
        return nullptr;
    }
};

QStringList launchExecutables(const vdf::Node &appInfo, bool windows)
{
    QStringList result;
    const vdf::Node *launch = appInfo.find({u"config", u"launch"});
    if (!launch) {
        return result;
    }
    const QLatin1String wantedOs = windows ? QLatin1String("windows") : QLatin1String("linux");
    for (const vdf::Node &entry : launch->children) {
        const QString executable = entry.valueOf(u"executable");
        if (executable.isEmpty()) {
            continue;
        }
        QString osList;
        if (const vdf::Node *config = entry.child(u"config")) {
            osList = config->valueOf(u"oslist");
        }
        if (osList.isEmpty() || osList.contains(wantedOs)) {
            result.append(executable);
        }
    }
    return result;
}

bool isGameType(const vdf::Node &appInfo)
{
    const vdf::Node *typeNode = appInfo.find({u"common", u"type"});
    const QString type = typeNode ? typeNode->value.toLower() : QString();
    return type.isEmpty() || type == QLatin1String("game") || type == QLatin1String("demo");
}

// Which runtime Steam uses, in Steam's order of precedence: the tool chosen
// for this game, Valve's assignment for it, the game's own Linux version,
// then the default tool for everything else.
void resolveRuntime(Game &game, const vdf::Node *appInfo, const QHash<QString, QString> &mapping, const vdf::Node *valveMappings,
                    const ToolCatalog &tools)
{
    const QString key = QString::number(game.appId);
    QString toolName = mapping.value(key);
    if (toolName.isEmpty() && valveMappings) {
        if (const vdf::Node *entry = valveMappings->child(key)) {
            toolName = entry->valueOf(u"tool");
        }
    }

    std::optional<bool> hasLinuxVersion;
    if (appInfo) {
        if (const vdf::Node *osList = appInfo->find({u"common", u"oslist"})) {
            hasLinuxVersion = osList->value.contains(QLatin1String("linux"));
        }
    }

    if (toolName.isEmpty() && hasLinuxVersion == false) {
        toolName = mapping.value(QStringLiteral("0"));
    }

    if (!toolName.isEmpty()) {
        const ToolInfo info = tools.lookup(toolName);
        bool proton = info.translatesWindows();
        if (!info.known) {
            // Not listed anywhere we can read; judge by the game and the name.
            proton = hasLinuxVersion != true || toolName.contains(QLatin1String("proton"), Qt::CaseInsensitive);
        }
        if (proton) {
            game.runtime = Runtime::Proton;
            game.tool = {toolName, info.displayName, info.installPath};
            return;
        }
        // A Linux-to-Linux tool is one of Steam's container runtimes.
        game.runtime = Runtime::Native;
        return;
    }

    if (hasLinuxVersion.has_value()) {
        game.runtime = *hasLinuxVersion ? Runtime::Native : Runtime::Proton;
        return;
    }

    // No cache entry: a Proton prefix shows the game has run through Proton.
    const QString prefix = game.libraryPath + QStringLiteral("/steamapps/compatdata/") + key + QStringLiteral("/pfx");
    game.runtime = QFileInfo(prefix).isDir() ? Runtime::Proton : Runtime::Unknown;
}

void addIfExists(QStringList &paths, const QString &path)
{
    if (QFileInfo::exists(path) && !paths.contains(path)) {
        paths.append(path);
    }
}

} // namespace

ScanOptions ScanOptions::fromEnvironment()
{
    ScanOptions options;
    options.homePath = QDir::homePath();
    options.compatToolDirs = {
        QStringLiteral("/usr/share/steam/compatibilitytools.d"),
        QStringLiteral("/usr/local/share/steam/compatibilitytools.d"),
    };
    const QString extra = qEnvironmentVariable("STEAM_EXTRA_COMPAT_TOOLS_PATHS");
    options.compatToolDirs += extra.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return options;
}

SteamScan scanSteam(const ScanOptions &options)
{
    SteamScan scan;
    scan.steamRoot = findSteamRoot(options.homePath);
    if (scan.steamRoot.isEmpty()) {
        // Watch where Steam would appear so a later install is noticed.
        addIfExists(scan.watchPaths, options.homePath + QStringLiteral("/.steam"));
        addIfExists(scan.watchPaths, options.homePath + QStringLiteral("/.local/share"));
        return scan;
    }
    scan.steamFound = true;
    const QString &root = scan.steamRoot;

    scan.libraries = readLibraries(root, scan.warnings);
    std::vector<Manifest> apps;
    QHash<quint32, Manifest> toolApps;
    for (const LibraryFolder &library : std::as_const(scan.libraries)) {
        if (library.available) {
            readManifests(library.path, apps, toolApps, scan.skipped, scan.warnings);
            addIfExists(scan.watchPaths, library.path + QStringLiteral("/steamapps"));
        }
    }

    // Load only the cache entries we need: our games and the Steam Play list.
    QSet<quint32> wanted{SteamPlayManifestAppId};
    for (const Manifest &app : apps) {
        wanted.insert(app.appId);
    }
    AppInfoFile appInfo;
    const QString appInfoPath = root + QStringLiteral("/appcache/appinfo.vdf");
    if (QFileInfo::exists(appInfoPath)) {
        QString problem;
        if (!readAppInfo(appInfoPath, wanted, appInfo, &problem)) {
            scan.warnings.append(QStringLiteral("%1: %2").arg(appInfoPath, problem));
        }
        scan.warnings += appInfo.warnings;
    }

    const auto manifestEntry = appInfo.apps.constFind(SteamPlayManifestAppId);
    const vdf::Node *steamPlay = manifestEntry != appInfo.apps.constEnd() ? &*manifestEntry : nullptr;

    ToolCatalog tools;
    tools.custom = readCustomTools(options.compatToolDirs + QStringList{root + QStringLiteral("/compatibilitytools.d")}, scan.warnings);
    tools.valveTools = steamPlay ? steamPlay->find({u"extended", u"compat_tools"}) : nullptr;
    tools.installedToolApps = toolApps;
    const vdf::Node *valveMappings = steamPlay ? steamPlay->find({u"extended", u"app_mappings"}) : nullptr;

    const QHash<QString, QString> mapping = readCompatMapping(root, scan.warnings);
    scan.accountId = readAccountId(root, scan.warnings);
    QHash<quint32, QString> launchOptions;
    if (scan.accountId != 0) {
        const QString userConfig = root + QStringLiteral("/userdata/%1/config").arg(scan.accountId);
        scan.localConfigPath = userConfig + QStringLiteral("/localconfig.vdf");
        launchOptions = readLaunchOptions(scan.localConfigPath, scan.warnings);
        addIfExists(scan.watchPaths, userConfig);
    }

    QSet<quint32> listed;
    QHash<QString, Fsr4Support> fsr4ByTool; // each tool's files read once
    for (const Manifest &app : apps) {
        if (app.appId == SteamworksRedistAppId) {
            scan.skipped.append({app.appId, app.name, QStringLiteral("Steam's shared redistributables, not a game")});
            continue;
        }
        if (listed.contains(app.appId)) {
            scan.skipped.append({app.appId, app.name, QStringLiteral("installed in two libraries; listed once")});
            continue;
        }
        const auto entry = appInfo.apps.constFind(app.appId);
        const vdf::Node *info = entry != appInfo.apps.constEnd() ? &*entry : nullptr;
        if (info && !isGameType(*info)) {
            const vdf::Node *type = info->find({u"common", u"type"});
            scan.skipped.append({app.appId, app.name, QStringLiteral("not a game (Steam type: %1)").arg(type ? type->value : QString())});
            continue;
        }
        listed.insert(app.appId);

        Game game;
        game.appId = app.appId;
        game.name = app.name.isEmpty() ? QStringLiteral("App %1").arg(app.appId) : app.name;
        game.libraryPath = app.libraryPath;
        game.installPath = app.installPath;
        game.launchOptions = launchOptions.value(app.appId);
        game.buildId = app.buildId;
        game.coverPath = findCover(root, scan.accountId, app.appId, listedCovers(info));
        resolveRuntime(game, info, mapping, valveMappings, tools);
        // Steam's store category 8 is "Valve Anti-Cheat enabled".
        const vdf::Node *vac = info ? info->find({u"common", u"category", u"category_8"}) : nullptr;
        if (vac && vac->value == QLatin1String("1")) {
            game.antiCheat.append(QStringLiteral("Valve Anti-Cheat"));
        }
        if (game.runtime == Runtime::Proton && !game.tool.installPath.isEmpty()) {
            auto known = fsr4ByTool.find(game.tool.installPath);
            if (known == fsr4ByTool.end()) {
                known = fsr4ByTool.insert(game.tool.installPath, readFsr4Support(game.tool.installPath));
            }
            game.tool.fsr4 = *known;
        }

        if (options.inspectGameFiles) {
            const bool windows = game.runtime == Runtime::Proton;
            const QStringList executables = info ? launchExecutables(*info, windows) : QStringList();
            const GameFileReport report = options.inspector ? options.inspector(game, executables, windows)
                                                            : inspectGameFiles(game.installPath, executables, windows);
            game.apis = report.apis;
            game.fsr31Dx12 = report.fsr31Dx12;
            game.fsr31Vulkan = report.fsr31Vulkan;
            for (const QString &name : report.antiCheat) {
                if (!game.antiCheat.contains(name)) {
                    game.antiCheat.append(name);
                }
            }
        }
        scan.games.append(game);
    }

    std::sort(scan.games.begin(), scan.games.end(), [](const Game &a, const Game &b) {
        const int order = a.name.localeAwareCompare(b.name);
        return order != 0 ? order < 0 : a.appId < b.appId;
    });

    addIfExists(scan.watchPaths, root + QStringLiteral("/steamapps"));
    addIfExists(scan.watchPaths, root + QStringLiteral("/config"));
    addIfExists(scan.watchPaths, root + QStringLiteral("/appcache"));
    for (const QString &folder : options.compatToolDirs) {
        addIfExists(scan.watchPaths, folder);
    }
    addIfExists(scan.watchPaths, root + QStringLiteral("/compatibilitytools.d"));
    return scan;
}

} // namespace gfxtune::steam
