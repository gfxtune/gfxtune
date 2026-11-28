// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Frame generation through lsfg-vk: finding it and Lossless Scaling, and the
// settings file gfxtune writes for it.

#include "framegen/LsfgVk.h"
#include "steam/SteamLibrary.h"
#include "support/FakeSteam.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace gfxtune;
using settings::GameSettings;

namespace
{

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

ino_t inodeOf(const QString &path)
{
    struct stat info{};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? info.st_ino : 0;
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Sets environment variables for one test and puts them back after it.
class ScopedEnvironment
{
public:
    void set(const char *name, const QByteArray &value)
    {
        m_saved.append({name, qgetenv(name), qEnvironmentVariableIsSet(name)});
        qputenv(name, value);
    }
    ~ScopedEnvironment()
    {
        for (const auto &[name, value, wasSet] : std::as_const(m_saved)) {
            wasSet ? qputenv(name, value) : qunsetenv(name);
        }
    }

private:
    struct Saved {
        const char *name;
        QByteArray value;
        bool wasSet;
    };
    QList<Saved> m_saved;
};

} // namespace

class FrameGenerationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writesLsfgVksFormat()
    {
        GameSettings g;
        g.frameGeneration = 3;
        g.flowScale = 70;
        g.frameGenerationPerformance = true;
        QCOMPARE(framegen::configText(g, s("/games/Steam Library/steamapps/common/Lossless Scaling/lsfg-vk.dll")),
                 QByteArray("# lsfg-vk's settings for one game, written by gfxtune from its own\n"
                            "# settings: changes made here are overwritten.\n"
                            "version = 2\n"
                            "\n[global]\n"
                            "dll = \"/games/Steam Library/steamapps/common/Lossless Scaling/lsfg-vk.dll\"\n"
                            "\n[[profile]]\n"
                            "name = \"gfxtune\"\n"
                            "active_in = []\n"
                            "multiplier = 3\n"
                            "flow_scale = 0.70\n"
                            "performance_mode = true\n"));
    }

    void leavesTheFileToLsfgVkWithoutAKnownFile()
    {
        GameSettings g;
        g.frameGeneration = 2;
        const QByteArray text = framegen::configText(g, {});
        QVERIFY(!text.contains("[global]"));
        QVERIFY(text.contains("multiplier = 2\nflow_scale = 1.00\nperformance_mode = false\n"));
    }

    void keepsToLsfgVksLimits()
    {
        GameSettings g; // frame generation off: the file still holds a valid profile
        g.flowScale = GameSettings::MinFlowScale;
        const QByteArray text = framegen::configText(g, {});
        QVERIFY(text.contains("multiplier = 2\n"));
        QVERIFY(text.contains("flow_scale = 0.25\n"));
    }

    // TOML basic strings: quotes and backslashes escaped, control
    // characters as \uXXXX, the rest as it is.
    void quotesAnyPath()
    {
        const QByteArray text = framegen::configText(GameSettings{}, s("/a \"b\"\\c\nd\té/x.dll"));
        QVERIFY2(text.contains("dll = \"/a \\\"b\\\"\\\\c\\u000ad\\u0009é/x.dll\"\n"), text.constData());
    }

    void writesOnlyWhatChanged()
    {
        QTemporaryDir dir;
        const QString path = framegen::configPath(dir.path(), 1003);
        QCOMPARE(path, dir.path() + s("/lsfg-vk/1003/conf.toml"));
        QString error;
        QVERIFY2(framegen::writeConfig(path, "version = 2\n", &error), qPrintable(error));
        QCOMPARE(read(path), QByteArray("version = 2\n"));
        const ino_t first = inodeOf(path);
        QVERIFY(first != 0);
        // The same text again: the file is left alone, so lsfg-vk does not reload.
        QVERIFY(framegen::writeConfig(path, "version = 2\n"));
        QCOMPARE(inodeOf(path), first);
        // A change arrives as a new file moved in place, which lsfg-vk watches for.
        QVERIFY(framegen::writeConfig(path, "version = 2\n# changed\n"));
        QCOMPARE(read(path), QByteArray("version = 2\n# changed\n"));
        QVERIFY(inodeOf(path) != first);
        QCOMPARE(QDir(QFileInfo(path).path()).entryList(QDir::Files | QDir::Hidden), QStringList{s("conf.toml")});
    }

    void saysWhenItCannotWrite()
    {
        QTemporaryDir dir;
        QVERIFY(test::writeFile(dir.filePath(s("taken")), "a file, not a folder"));
        QString error;
        QVERIFY(!framegen::writeConfig(dir.filePath(s("taken/1/conf.toml")), "version = 2\n", &error));
        QVERIFY(!error.isEmpty());
    }

    void remembersLosslessScalingsFile()
    {
        QTemporaryDir dir;
        QCOMPARE(framegen::readDllNote(dir.path()), QString());
        QVERIFY(framegen::writeDllNote(dir.path(), s("/library two/Lossless Scaling/lsfg-vk.dll")));
        QCOMPARE(framegen::readDllNote(dir.path()), s("/library two/Lossless Scaling/lsfg-vk.dll"));
        // Gone from Steam: the note goes too.
        QVERIFY(framegen::writeDllNote(dir.path(), {}));
        QVERIFY(!QFile::exists(framegen::dllNotePath(dir.path())));
        QVERIFY(framegen::writeDllNote(dir.path(), {}));
    }

    void findsTheLayerByName()
    {
        QTemporaryDir dir;
        const QString first = dir.filePath(s("first"));
        const QString second = dir.filePath(s("second"));
        QVERIFY(test::writeFile(first + s("/MangoHud.x86_64.json"), R"({"layer": {"name": "VK_LAYER_MANGOHUD_overlay_x86_64"}})"));
        QVERIFY(test::writeFile(first + s("/broken.json"), "{\"layer\": {\"name\": \"VK_LAYER_LSFGVK_frame"));
        QVERIFY(QDir().mkpath(first + s("/folder.json")));
        QVERIFY(test::writeFile(first + s("/notes.txt"), "VK_LAYER_LSFGVK_frame_generation"));
        QVERIFY(!framegen::layerInstalled({first, second, dir.filePath(s("missing"))}));

        QVERIFY(test::writeFile(second + s("/VkLayer_LSFGVK_frame_generation.json"),
                                R"({"file_format_version": "1.1.0", "layer": {"name": "VK_LAYER_LSFGVK_frame_generation"}})"));
        QVERIFY(framegen::layerInstalled({first, second}));
        QVERIFY(!framegen::layerInstalled({first}));

        // Manifests may also list several layers.
        QVERIFY(test::writeFile(first + s("/many.json"), R"({"file_format_version": "1.2.0", "layers": [
                                    {"name": "VK_LAYER_other"}, {"name": "VK_LAYER_LSFGVK_frame_generation"}]})"));
        QVERIFY(framegen::layerInstalled({first}));
    }

    void looksWhereTheVulkanLoaderLooks()
    {
        ScopedEnvironment environment;
        environment.set("XDG_CONFIG_HOME", "/home/user/.config");
        environment.set("XDG_CONFIG_DIRS", "/etc/xdg:/etc/xdg");
        environment.set("XDG_DATA_HOME", "/home/user/.local/share");
        environment.set("XDG_DATA_DIRS", "/usr/local/share::/usr/share");
        QCOMPARE(framegen::implicitLayerDirs(),
                 (QStringList{
                     s("/home/user/.config/vulkan/implicit_layer.d"),
                     s("/etc/xdg/vulkan/implicit_layer.d"),
                     s("/etc/vulkan/implicit_layer.d"),
                     s("/home/user/.local/share/vulkan/implicit_layer.d"),
                     s("/usr/local/share/vulkan/implicit_layer.d"),
                     s("/usr/share/vulkan/implicit_layer.d"),
                 }));
        environment.set("XDG_DATA_DIRS", "");
        QVERIFY(framegen::implicitLayerDirs().endsWith(s("/usr/share/vulkan/implicit_layer.d")));
    }

    void findsLosslessScalingOnItsBranch()
    {
        test::FakeSteam steam;
        steam.build();
        steam.losslessScaling(steam.lib2, s("lsfg-vk"), s("lsfg-vk.dll"));
        const steam::SteamScan scan = steam::scanSteam(steam.options());
        QVERIFY(scan.losslessScaling.installed);
        QCOMPARE(scan.losslessScaling.branch, s("lsfg-vk"));
        QCOMPARE(scan.losslessScaling.dll, steam.lib2 + s("/steamapps/common/Lossless Scaling/lsfg-vk.dll"));
        // A tool, never in the game list.
        for (const steam::Game &game : scan.games) {
            QVERIFY(game.appId != framegen::LosslessScalingAppId);
        }
        const auto skipped =
            std::ranges::find_if(scan.skipped, [](const steam::SkippedApp &app) { return app.appId == framegen::LosslessScalingAppId; });
        QVERIFY(skipped != scan.skipped.end());
        QVERIFY(skipped->reason.contains(QLatin1String("lsfg-vk")));
    }

    void findsLosslessScalingOffItsBranch()
    {
        test::FakeSteam steam;
        steam.build();
        steam.losslessScaling(steam.root, {}, s("Lossless.dll"));
        const steam::SteamScan scan = steam::scanSteam(steam.options());
        QVERIFY(scan.losslessScaling.installed);
        QCOMPARE(scan.losslessScaling.branch, QString());
        QCOMPARE(scan.losslessScaling.dll, steam.root + s("/steamapps/common/Lossless Scaling/Lossless.dll"));
    }

    // A branch chosen in Steam but still downloading is in UserConfig only.
    void readsAChosenBranchStillDownloading()
    {
        test::FakeSteam steam;
        steam.build();
        steam.losslessScaling(steam.root, {}, {});
        steam.text(steam.root + s("/steamapps/appmanifest_993090.acf"),
                   test::file({test::section(s("AppState"),
                                             {test::value(s("appid"), s("993090")), test::value(s("name"), s("Lossless Scaling")),
                                              test::value(s("StateFlags"), s("4")), test::value(s("installdir"), s("Lossless Scaling")),
                                              test::section(s("UserConfig"), {test::value(s("BetaKey"), s("lsfg-vk"))})})}));
        const steam::SteamScan scan = steam::scanSteam(steam.options());
        QCOMPARE(scan.losslessScaling.branch, s("lsfg-vk"));
        QCOMPARE(scan.losslessScaling.dll, QString());
    }

    void reportsLosslessScalingMissing()
    {
        test::FakeSteam steam;
        steam.build();
        const steam::SteamScan scan = steam::scanSteam(steam.options());
        QVERIFY(!scan.losslessScaling.installed);
        QVERIFY(scan.losslessScaling.dll.isEmpty());
    }
};

QTEST_GUILESS_MAIN(FrameGenerationTest)
#include "tst_framegen.moc"
