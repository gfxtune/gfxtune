// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameFiles.h"

#include "binaries/ExecutableImports.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace gfxtune::steam
{

namespace
{

// A game folder can hold hundreds of thousands of files. These bounds keep a
// scan short; the files that matter sit near the top.
constexpr int MaxEntries = 50'000;
constexpr int MaxDepth = 6;
constexpr int MaxFallbackPrograms = 8;

struct Program {
    QString path;
    qint64 size = 0;
};

struct FolderIndex {
    // Lower-case path relative to the install folder -> real path.
    QHash<QString, QString> files;
    std::vector<Program> programs;
    bool fsr31Dx12 = false;
    bool fsr31Vulkan = false;
    bool unity = false; // ships Unity's Windows player
    QStringList antiCheat;
};

void addAntiCheat(FolderIndex &index, const QString &name)
{
    if (!name.isEmpty() && !index.antiCheat.contains(name)) {
        index.antiCheat.append(name);
    }
}

bool isLinuxProgram(const QFileInfo &info)
{
    if (!info.isExecutable()) {
        return false;
    }
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(4) == QByteArrayLiteral("\x7F" "ELF");
}

void indexFile(const QFileInfo &info, const QString &relative, bool windowsGame, FolderIndex &index)
{
    const QString lower = relative.toLower();
    const QString fileName = info.fileName().toLower();

    if (fileName == QLatin1String("amd_fidelityfx_dx12.dll")) {
        index.fsr31Dx12 = true;
    } else if (fileName == QLatin1String("amd_fidelityfx_vk.dll")) {
        index.fsr31Vulkan = true;
    } else if (fileName == QLatin1String("unityplayer.dll")) {
        index.unity = true;
    }
    addAntiCheat(index, antiCheatOfFile(fileName));

    if (windowsGame) {
        if (fileName.endsWith(QLatin1String(".exe")) || fileName.endsWith(QLatin1String(".dll"))) {
            index.files.insert(lower, info.filePath());
        }
        if (fileName.endsWith(QLatin1String(".exe"))) {
            index.programs.push_back({info.filePath(), info.size()});
        }
    } else if (fileName.contains(QLatin1String(".so"))) {
        index.files.insert(lower, info.filePath());
    } else if (isLinuxProgram(info)) {
        index.files.insert(lower, info.filePath());
        index.programs.push_back({info.filePath(), info.size()});
    }
}

// Breadth first, so the shallow folders where programs live are covered
// before a deep folder of assets can use up the entry budget.
FolderIndex indexFolder(const QString &root, bool windowsGame)
{
    FolderIndex index;
    const QDir base(root);
    std::vector<std::pair<QString, int>> queue{{root, 0}};
    int seen = 0;
    for (std::size_t next = 0; next < queue.size() && seen < MaxEntries; ++next) {
        const auto [folder, depth] = queue[next];
        const QFileInfoList entries =
            QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (++seen > MaxEntries) {
                break;
            }
            if (info.isDir()) {
                addAntiCheat(index, antiCheatOfFolder(info.fileName().toLower()));
                if (!info.isSymLink() && depth + 1 < MaxDepth) {
                    queue.emplace_back(info.filePath(), depth + 1);
                }
                continue;
            }
            indexFile(info, base.relativeFilePath(info.filePath()), windowsGame, index);
        }
    }
    return index;
}

QString normalise(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    path = QDir::cleanPath(path);
    while (path.startsWith(QLatin1String("./"))) {
        path.remove(0, 2);
    }
    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    return path.toLower();
}

// The program's own imports, plus those of libraries it imports that ship in
// the same folder (engines such as Unity keep the renderer there).
GraphicsApis apisOfProgram(const QString &programPath, const FolderIndex &index, const QString &root)
{
    binaries::Imports imports;
    if (!binaries::readImports(programPath, imports)) {
        return {};
    }
    GraphicsApis apis = apisFromLibraries(imports.libraries);

    const QString folder = normalise(QDir(root).relativeFilePath(QFileInfo(programPath).path()));
    for (const QString &library : std::as_const(imports.libraries)) {
        const QString key = folder == QLatin1String(".") ? library.toLower() : folder + QLatin1Char('/') + library.toLower();
        const auto found = index.files.constFind(key);
        if (found == index.files.constEnd() || *found == programPath) {
            continue;
        }
        binaries::Imports nested;
        if (binaries::readImports(*found, nested)) {
            apis |= apisFromLibraries(nested.libraries);
        }
    }
    return apis;
}

constexpr auto DirectXApis = GraphicsApi::DirectX8 | GraphicsApi::DirectX9 | GraphicsApi::DirectX10
    | GraphicsApi::DirectX11 | GraphicsApi::DirectX12;

// Windows programs often link OpenGL without drawing with it. Seen on real
// games: Unity's player links OpenGL and loads Direct3D 11, its Windows
// default, only once the game runs; Unreal games link both.
GraphicsApis refineWindowsGuess(GraphicsApis apis, const FolderIndex &index)
{
    if (apis & DirectXApis) {
        apis.setFlag(GraphicsApi::OpenGL, false);
    } else if (index.unity && !(apis & GraphicsApi::Vulkan)) {
        apis = GraphicsApi::DirectX11;
    }
    return apis;
}

} // namespace

GraphicsApis apisFromLibraries(const QStringList &libraries)
{
    GraphicsApis apis;
    for (const QString &name : libraries) {
        const QString lib = name.toLower();
        if (lib == QLatin1String("d3d12.dll")) {
            apis |= GraphicsApi::DirectX12;
        } else if (lib == QLatin1String("d3d11.dll")) {
            apis |= GraphicsApi::DirectX11;
        } else if (lib == QLatin1String("d3d10.dll") || lib == QLatin1String("d3d10_1.dll") || lib == QLatin1String("d3d10core.dll")) {
            apis |= GraphicsApi::DirectX10;
        } else if (lib == QLatin1String("d3d9.dll")) {
            apis |= GraphicsApi::DirectX9;
        } else if (lib == QLatin1String("d3d8.dll")) {
            apis |= GraphicsApi::DirectX8;
        } else if (lib == QLatin1String("vulkan-1.dll") || lib.startsWith(QLatin1String("libvulkan.so"))) {
            apis |= GraphicsApi::Vulkan;
        } else if (lib == QLatin1String("opengl32.dll") || lib.startsWith(QLatin1String("libgl.so")) || lib.startsWith(QLatin1String("libglx.so"))
                   || lib.startsWith(QLatin1String("libopengl.so")) || lib.startsWith(QLatin1String("libegl.so"))
                   || lib.startsWith(QLatin1String("libglesv2.so"))) {
            apis |= GraphicsApi::OpenGL;
        }
    }
    return apis;
}

QString antiCheatOfFile(const QString &fileName)
{
    const auto is = [&fileName](std::initializer_list<const char *> names) {
        return std::any_of(names.begin(), names.end(), [&fileName](const char *name) { return fileName == QLatin1String(name); });
    };
    if (fileName.startsWith(QLatin1String("easyanticheat")) || is({"start_protected_game.exe"})) {
        return QStringLiteral("Easy Anti-Cheat");
    }
    if (is({"beservice.exe", "beservice_x64.exe", "beclient.dll", "beclient_x64.dll", "beclient_x64.so"})) {
        return QStringLiteral("BattlEye");
    }
    const bool program = fileName.endsWith(QLatin1String(".exe")) || fileName.endsWith(QLatin1String(".dll"))
        || fileName.endsWith(QLatin1String(".sys"));
    if (program && (fileName.startsWith(QLatin1String("sguard")) || fileName.startsWith(QLatin1String("ace-")))) {
        return QStringLiteral("Anti-Cheat Expert");
    }
    if (fileName.startsWith(QLatin1String("mhyprot")) || fileName.startsWith(QLatin1String("hoyokprotect")) || is({"mhypbase.dll"})) {
        return QStringLiteral("miHoYo Protect");
    }
    if (is({"gamemon.des", "gamemon64.des"})) {
        return QStringLiteral("nProtect GameGuard");
    }
    if (fileName.endsWith(QLatin1String(".xem"))) {
        return QStringLiteral("XIGNCODE3");
    }
    if (fileName.startsWith(QLatin1String("eaanticheat"))) {
        return QStringLiteral("EA Anti-Cheat");
    }
    if (fileName.endsWith(QLatin1String(".sys"))) {
        return QStringLiteral("a kernel driver, most likely anti-cheat");
    }
    return {};
}

QString antiCheatOfFolder(const QString &folderName)
{
    if (folderName.startsWith(QLatin1String("easyanticheat"))) {
        return QStringLiteral("Easy Anti-Cheat");
    }
    if (folderName == QLatin1String("battleye")) {
        return QStringLiteral("BattlEye");
    }
    if (folderName == QLatin1String("gameguard")) {
        return QStringLiteral("nProtect GameGuard");
    }
    if (folderName == QLatin1String("eaanticheat")) {
        return QStringLiteral("EA Anti-Cheat");
    }
    static const QRegularExpression generic(QStringLiteral("anti[-_ ]?cheat"));
    if (generic.match(folderName).hasMatch()) {
        return QStringLiteral("anti-cheat");
    }
    return {};
}

GameFileReport inspectGameFiles(const QString &installPath, const QStringList &executables, bool windowsGame)
{
    GameFileReport report;
    if (!QFileInfo(installPath).isDir()) {
        return report;
    }
    const FolderIndex index = indexFolder(installPath, windowsGame);
    report.fsr31Dx12 = index.fsr31Dx12;
    report.fsr31Vulkan = index.fsr31Vulkan;
    report.antiCheat = index.antiCheat;

    QStringList checked;
    for (const QString &executable : executables) {
        const auto found = index.files.constFind(normalise(executable));
        if (found == index.files.constEnd() || checked.contains(*found)) {
            continue;
        }
        checked.append(*found);
        report.apis |= apisOfProgram(*found, index, installPath);
    }
    if (report.apis) {
        if (windowsGame) {
            report.apis = refineWindowsGuess(report.apis, index);
        }
        return report;
    }

    // The launch entry is often a small launcher. Try the largest programs,
    // which are usually the game itself.
    std::vector<Program> programs = index.programs;
    std::sort(programs.begin(), programs.end(), [](const Program &a, const Program &b) { return a.size > b.size; });
    int tried = 0;
    for (const Program &program : programs) {
        if (checked.contains(program.path)) {
            continue;
        }
        if (tried++ >= MaxFallbackPrograms) {
            break;
        }
        const GraphicsApis apis = apisOfProgram(program.path, index, installPath);
        if (apis) {
            report.apis = apis;
            break;
        }
    }
    if (windowsGame) {
        report.apis = refineWindowsGuess(report.apis, index);
    }
    return report;
}

} // namespace gfxtune::steam
