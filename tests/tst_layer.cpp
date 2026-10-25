// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// The in-game filters on a real Vulkan device: the filter pass against the
// C++ reference pixel by pixel, then the layer itself, loaded through the
// Vulkan loader into a small "game" that presents to a headless swapchain.
// In the development container that device is Mesa's software rasteriser;
// on a desktop it is the graphics card. Without any Vulkan device the tests
// are skipped. Khronos' validation layer checks every call when installed.

#include "FilterPass.h"
#include "LayerSettings.h"
#include "VulkanFunctions.h"
#include "filters/Filters.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <vulkan/vulkan.h>

#include <cstring>
#include <functional>
#include <optional>
#include <vector>

using namespace gfxtune;
using filters::Rgb;
using filters::ShaderParams;
using filters::Values;

namespace
{

constexpr uint32_t Width = 24;
constexpr uint32_t Height = 18;

// A fixed, varied picture: smooth gradients, hard edges, black and white.
std::vector<Rgb> testPicture()
{
    std::vector<Rgb> pixels(Width * Height);
    uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFF) / 255.0f;
    };
    for (uint32_t y = 0; y < Height; ++y) {
        for (uint32_t x = 0; x < Width; ++x) {
            Rgb &p = pixels[y * Width + x];
            if (x < 6) {
                p = {float(x * 40) / 255.0f, float(y * 14) / 255.0f, 0.5f}; // gradient
            } else if (x < 12) {
                const float v = ((x + y) % 2) ? 1.0f : 0.0f; // checkerboard
                p = {v, v, v};
            } else {
                p = {next(), next(), next()}; // noise
            }
        }
    }
    return pixels;
}

uint8_t toByte(float value)
{
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// What the shader should produce, from the bytes the GPU actually read,
// optionally with every input value moved by `shift`.
std::vector<Rgb> reference(const std::vector<Rgb> &picture, const ShaderParams &p, float shift = 0.0f)
{
    std::vector<Rgb> input(picture.size());
    for (std::size_t i = 0; i < picture.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            input[i][size_t(c)] = std::clamp(float(toByte(picture[i][size_t(c)])) / 255.0f + shift, 0.0f, 1.0f);
        }
    }
    const auto at = [&input](int x, int y) {
        return input[size_t(std::clamp(y, 0, int(Height) - 1)) * Width + size_t(std::clamp(x, 0, int(Width) - 1))];
    };
    std::vector<Rgb> out(input.size());
    for (int y = 0; y < int(Height); ++y) {
        for (int x = 0; x < int(Width); ++x) {
            Rgb c = at(x, y);
            if (p.flags & ShaderParams::Sharpen) {
                std::array<Rgb, 9> n;
                for (int i = 0; i < 9; ++i) {
                    n[size_t(i)] = at(x + i % 3 - 1, y + i / 3 - 1);
                }
                c = filters::sharpen(n, p.sharpness);
            }
            if (p.flags & ShaderParams::Colour) {
                c = filters::adjustColour(c, p);
            }
            out[size_t(y) * Width + size_t(x)] = c;
        }
    }
    return out;
}

bool bgra(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

QByteArray pack(const std::vector<Rgb> &pixels, VkFormat format)
{
    QByteArray bytes;
    for (const Rgb &p : pixels) {
        const uint8_t r = toByte(p[0]), g = toByte(p[1]), b = toByte(p[2]);
        if (bgra(format)) {
            bytes.append(char(b)).append(char(g)).append(char(r)).append(char(255));
        } else {
            bytes.append(char(r)).append(char(g)).append(char(b)).append(char(255));
        }
    }
    return bytes;
}

// The largest distance of any channel from the expected range (the
// references in `expected`), and where it exceeds `tolerance`.
QString compare(const QByteArray &actual, const std::vector<std::vector<Rgb>> &expected, VkFormat format, int tolerance, int *worst)
{
    *worst = 0;
    QString where;
    std::vector<QByteArray> wants;
    for (const std::vector<Rgb> &e : expected) {
        wants.push_back(pack(e, format));
    }
    const QByteArray &want = wants.front();
    if (actual.size() != want.size()) {
        *worst = 255;
        return QStringLiteral("got %1 bytes, expected %2").arg(actual.size()).arg(want.size());
    }
    for (qsizetype i = 0; i < want.size(); ++i) {
        if (i % 4 == 3) {
            continue; // alpha
        }
        int low = 255, high = 0;
        for (const QByteArray &w : wants) {
            low = std::min(low, int(uint8_t(w[i])));
            high = std::max(high, int(uint8_t(w[i])));
        }
        const int got = int(uint8_t(actual[i]));
        const int diff = got < low ? low - got : (got > high ? got - high : 0);
        if (diff > *worst) {
            *worst = diff;
            where = QStringLiteral("pixel (%1, %2) channel %3: got %4, expected %5")
                        .arg((i / 4) % Width)
                        .arg((i / 4) / Width)
                        .arg(i % 4)
                        .arg(got)
                        .arg(low == high ? QString::number(low) : QStringLiteral("%1 to %2").arg(low).arg(high));
        }
    }
    return *worst > tolerance ? where : QString();
}

// A Vulkan instance and device from the system loader, with optional layers.
struct Vulkan {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    layer::DeviceFunctions vk;
    QStringList problems; // validation errors
    QString why;          // why it could not be set up

    static QStringList availableLayers()
    {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        QStringList names;
        for (const VkLayerProperties &l : layers) {
            names << QString::fromUtf8(l.layerName);
        }
        return names;
    }

    static bool hasInstanceExtension(const char *name)
    {
        uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> list(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, list.data());
        return std::any_of(list.begin(), list.end(), [name](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, name) == 0; });
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            static_cast<Vulkan *>(user)->problems << QString::fromUtf8(data->pMessage);
        }
        return VK_FALSE;
    }

    bool create(const QStringList &layers, bool headless)
    {
        std::vector<QByteArray> layerNames;
        for (const QString &l : layers) {
            layerNames.push_back(l.toUtf8());
        }
        std::vector<const char *> layerPointers;
        for (const QByteArray &l : layerNames) {
            layerPointers.push_back(l.constData());
        }
        std::vector<const char *> extensions;
        const bool debug = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debug) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        if (headless) {
            if (!hasInstanceExtension(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME)) {
                why = QStringLiteral("no headless surface support");
                return false;
            }
            extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
        }

        auto app = layer::vkStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "gfxtune tests";
        app.apiVersion = VK_API_VERSION_1_1;
        auto info = layer::vkStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        info.pApplicationInfo = &app;
        info.enabledLayerCount = uint32_t(layerPointers.size());
        info.ppEnabledLayerNames = layerPointers.data();
        info.enabledExtensionCount = uint32_t(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
            why = QStringLiteral("no Vulkan driver");
            return false;
        }
        if (debug) {
            auto messengerInfo = layer::vkStruct<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            messengerInfo.pfnUserCallback = &onMessage;
            messengerInfo.pUserData = this;
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            create(instance, &messengerInfo, nullptr, &messenger);
        }

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
            for (uint32_t i = 0; i < families; ++i) {
                if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical) {
                break;
            }
        }
        if (!physical) {
            why = QStringLiteral("no Vulkan device that can draw");
            return false;
        }

        if (headless) {
            auto surfaceInfo = layer::vkStruct<VkHeadlessSurfaceCreateInfoEXT>(VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT);
            const auto createSurface = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
            VkBool32 supported = VK_FALSE;
            if (!createSurface || createSurface(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS
                || vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &supported) != VK_SUCCESS || !supported) {
                why = QStringLiteral("the device cannot present to a headless surface");
                return false;
            }
        }

        const float priority = 1.0f;
        auto queueInfo = layer::vkStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char *swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        auto deviceInfo = layer::vkStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = headless ? 1 : 0;
        deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
        if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
            why = QStringLiteral("could not create a device");
            return false;
        }
        vk.load(device, vkGetDeviceProcAddr);
        vk.GetDeviceQueue(device, family, 0, &queue);
        auto poolInfo = layer::vkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = family;
        vk.CreateCommandPool(device, &poolInfo, nullptr, &pool);
        return true;
    }

    ~Vulkan()
    {
        if (device) {
            vk.DeviceWaitIdle(device);
            vk.DestroyCommandPool(device, pool, nullptr);
            vk.DestroyDevice(device, nullptr);
        }
        if (surface) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (messenger) {
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))(
                instance, messenger, nullptr);
        }
        if (instance) {
            vkDestroyInstance(instance, nullptr);
        }
    }

    bool formatUsable(VkFormat format, VkFormatFeatureFlags features) const
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical, format, &props);
        return (props.optimalTilingFeatures & features) == features;
    }

    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags wanted) const
    {
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(physical, &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((allowed & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
    };

    Buffer hostBuffer(VkDeviceSize size)
    {
        Buffer b;
        auto info = layer::vkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(device, &info, nullptr, &b.buffer);
        VkMemoryRequirements needs{};
        vkGetBufferMemoryRequirements(device, b.buffer, &needs);
        auto allocate = layer::vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocate.allocationSize = needs.size;
        allocate.memoryTypeIndex = memoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &allocate, nullptr, &b.memory);
        vkBindBufferMemory(device, b.buffer, b.memory, 0);
        vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
        return b;
    }

    void free(Buffer &b)
    {
        vkUnmapMemory(device, b.memory);
        vkDestroyBuffer(device, b.buffer, nullptr);
        vkFreeMemory(device, b.memory, nullptr);
        b = Buffer{};
    }

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    Image image(VkFormat format, VkImageUsageFlags usage)
    {
        Image i;
        auto info = layer::vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {Width, Height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        vk.CreateImage(device, &info, nullptr, &i.image);
        VkMemoryRequirements needs{};
        vk.GetImageMemoryRequirements(device, i.image, &needs);
        auto allocate = layer::vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocate.allocationSize = needs.size;
        allocate.memoryTypeIndex = memoryType(needs.memoryTypeBits, 0);
        vk.AllocateMemory(device, &allocate, nullptr, &i.memory);
        vk.BindImageMemory(device, i.image, i.memory, 0);
        return i;
    }

    void free(Image &i)
    {
        vk.DestroyImage(device, i.image, nullptr);
        vk.FreeMemory(device, i.memory, nullptr);
        i = Image{};
    }

    // Records with `body`, submits, and waits.
    bool run(const std::function<void(VkCommandBuffer)> &body, VkSemaphore signal = VK_NULL_HANDLE)
    {
        auto allocate = layer::vkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocate.commandPool = pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vk.AllocateCommandBuffers(device, &allocate, &cmd);
        auto begin = layer::vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.BeginCommandBuffer(cmd, &begin);
        body(cmd);
        vk.EndCommandBuffer(cmd);
        auto submit = layer::vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = signal ? 1 : 0;
        submit.pSignalSemaphores = &signal;
        const bool ok = vk.QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vk.DeviceWaitIdle(device) == VK_SUCCESS;
        vk.FreeCommandBuffers(device, pool, 1, &cmd);
        return ok;
    }

    void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to)
    {
        auto b = layer::vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    static VkBufferImageCopy wholeImage()
    {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {Width, Height, 1};
        return region;
    }
};

ShaderParams params(const Values &values, VkFormat format)
{
    return filters::shaderParams(values, layer::isSrgbFormat(format));
}

void writeSettings(const QString &path, const Values &v)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QStringLiteral("[filters]\nenabled=%1\nsharpening=%2\nbrightness=%3\ncontrast=%4\ngamma=%5\n"
                              "saturation=%6\nvibrance=%7\ntemperature=%8\n")
                   .arg(v.enabled ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(v.sharpening)
                   .arg(v.brightness)
                   .arg(v.contrast)
                   .arg(v.gamma)
                   .arg(v.saturation)
                   .arg(v.vibrance)
                   .arg(v.temperature)
                   .toUtf8());
}

Values sample(int which)
{
    Values v;
    switch (which) {
    case 0: // sharpening only
        v.sharpening = 60;
        break;
    case 1: // strongest sharpening
        v.sharpening = 100;
        break;
    case 2: // every colour control at once
        v.brightness = 20;
        v.contrast = 30;
        v.gamma = 140;
        v.saturation = -40;
        v.vibrance = 70;
        v.temperature = 50;
        break;
    case 3: // the other way, with sharpening
        v.sharpening = 25;
        v.brightness = -50;
        v.contrast = -50;
        v.gamma = 50;
        v.saturation = 100;
        v.vibrance = -100;
        v.temperature = -100;
        break;
    default: // switched off: the picture must pass through untouched
        v.enabled = false;
        v.sharpening = 80;
        v.contrast = 40;
        break;
    }
    return v;
}

} // namespace

class LayerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_settings;
    QString m_report;

    QString layerDir() const { return QStringLiteral(GFXTUNE_LAYER_MANIFEST_DIR); }

private Q_SLOTS:
    void initTestCase()
    {
        // The layer reads these once per process, at its first frame.
        m_settings = m_dir.filePath(QStringLiteral("game.ini"));
        m_report = m_dir.filePath(QStringLiteral("game.filters"));
        qputenv("GFXTUNE_FILTERS_SETTINGS", m_settings.toUtf8());
        qputenv("GFXTUNE_FILTERS_REPORT", m_report.toUtf8());
        // Only the layer this build made, as an explicit layer, so it can sit
        // above the validation layer and be checked by it.
        qputenv("VK_ADD_LAYER_PATH", layerDir().toUtf8());
        qunsetenv("GFXTUNE_FILTERS");
    }

    void readsTheSettingsFile()
    {
        const Values v = layer::parseFilterSettings(
            "[game]\nsharpening=99\n"
            "[filters]\nenabled=false\nsharpening=55\nbrightness=-12\ncontrast= 7 \ngamma=180\n"
            "saturation=-100\nvibrance=101\ntemperature=x\nunknown=3\n; comment\n"
            "[gamescope]\nsharpness=4\n"
            // The same names in another section are not filters.
            "[other]\nsharpening=7\ncontrast=9\n");
        QCOMPARE(v.enabled, false);
        QCOMPARE(v.sharpening, 55);
        QCOMPARE(v.brightness, -12);
        QCOMPARE(v.contrast, 7);
        QCOMPARE(v.gamma, 180);
        QCOMPARE(v.saturation, -100);
        QCOMPARE(v.vibrance, filters::Vibrance.neutral);       // out of range
        QCOMPARE(v.temperature, filters::Temperature.neutral); // not a number
        QCOMPARE(layer::parseFilterSettings(""), Values{});
        QCOMPARE(layer::parseFilterSettings("garbage\x01\xFF[[["), Values{});
    }

    void followsTheSettingsFileAsItChanges()
    {
        const QString path = m_dir.filePath(QStringLiteral("follow.ini"));
        layer::SettingsFile file(path.toStdString(), std::chrono::milliseconds(0));
        QCOMPARE(file.values(), Values{}); // no file yet: nothing to draw
        Values v;
        v.sharpening = 30;
        writeSettings(path, v);
        QCOMPARE(file.values().sharpening, 30);
        v.sharpening = 70;
        v.contrast = -5;
        writeSettings(path, v);
        QCOMPARE(file.values().sharpening, 70);
        QCOMPARE(file.values().contrast, -5);
        QFile::remove(path); // "Reset" deleted it
        QCOMPARE(file.values(), Values{});

        // Between checks, the file is not looked at.
        layer::SettingsFile slow(path.toStdString(), std::chrono::hours(1));
        writeSettings(path, v);
        QCOMPARE(slow.values().sharpening, 70);
        v.sharpening = 10;
        writeSettings(path, v);
        QCOMPARE(slow.values().sharpening, 70);
    }

    void writesItsReport()
    {
        const QString path = m_dir.filePath(QStringLiteral("report.filters"));
        QVERIFY(layer::writeReport(path.toStdString(), true, "1920×1080, B8G8R8A8_SRGB"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(QLatin1String("state=running\n")));
        QVERIFY(text.contains(QStringLiteral("detail=1920×1080, B8G8R8A8_SRGB\n")));
        QVERIFY(!layer::writeReport(m_dir.filePath(QStringLiteral("missing/dir/x")).toStdString(), true, "x"));
        QVERIFY(!layer::writeReport({}, true, "x"));
    }

    void drawsWhatTheReferenceSays_data()
    {
        QTest::addColumn<int>("format");
        QTest::addColumn<int>("which");
        for (const auto &[name, format] : {std::pair{"UNORM", VK_FORMAT_R8G8B8A8_UNORM}, std::pair{"sRGB", VK_FORMAT_B8G8R8A8_SRGB}}) {
            for (int which = 0; which <= 4; ++which) {
                QTest::addRow("%s, case %d", name, which) << int(format) << which;
            }
        }
    }

    void drawsWhatTheReferenceSays()
    {
        QFETCH(int, format);
        QFETCH(int, which);
        const auto f = VkFormat(format);
        const QStringList layers = Vulkan::availableLayers().contains(QStringLiteral("VK_LAYER_KHRONOS_validation"))
                                       ? QStringList{QStringLiteral("VK_LAYER_KHRONOS_validation")}
                                       : QStringList{};
        Vulkan v;
        if (!v.create(layers, false)) {
            QSKIP(qPrintable(v.why));
        }
        if (!v.formatUsable(f, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            QSKIP("the device cannot use this format");
        }

        std::string error;
        const auto pass = layer::FilterPass::create(v.vk, v.device, f, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &error);
        QVERIFY2(pass, error.c_str());
        Vulkan::Image source = v.image(f, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        Vulkan::Image target = v.image(f, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        layer::FilterPass::Target t;
        QVERIFY2(pass->addTarget(source.image, target.image, {Width, Height}, t, &error), error.c_str());

        const std::vector<Rgb> picture = testPicture();
        const QByteArray input = pack(picture, f);
        Vulkan::Buffer upload = v.hostBuffer(VkDeviceSize(input.size()));
        Vulkan::Buffer download = v.hostBuffer(VkDeviceSize(input.size()));
        std::memcpy(upload.mapped, input.constData(), size_t(input.size()));

        const ShaderParams p = params(sample(which), f);
        const VkBufferImageCopy region = Vulkan::wholeImage();
        QVERIFY(v.run([&](VkCommandBuffer cmd) {
            v.barrier(cmd, source.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(cmd, upload.buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            v.barrier(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            v.barrier(cmd, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            pass->record(cmd, t, p);
            vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, download.buffer, 1, &region);
        }));

        const QByteArray output(static_cast<const char *>(download.mapped), input.size());
        int worst = 0;
        // GPUs compute pow() and friends a little differently from the C++
        // library; two steps in 255 allow for that, and no more.
        // sRGB formats are decoded and encoded again on the way; GPUs do
        // that to about half a step, which steep settings can magnify. The
        // result must lie between the references for inputs half a step
        // lower and higher.
        std::vector<std::vector<Rgb>> expected{reference(picture, p)};
        if (layer::isSrgbFormat(f)) {
            expected.push_back(reference(picture, p, -0.5f / 255.0f));
            expected.push_back(reference(picture, p, 0.5f / 255.0f));
        }
        const QString mismatch = compare(output, expected, f, 2, &worst);
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
        if (!(p.flags & (ShaderParams::Sharpen | ShaderParams::Colour)) && f == VK_FORMAT_R8G8B8A8_UNORM) {
            QCOMPARE(worst, 0); // switched off: bit for bit the game's picture
        }
        if (which == 1) {
            // Strong sharpening must actually change the picture.
            int changed = 0;
            for (qsizetype i = 0; i < input.size(); ++i) {
                changed += std::abs(int(uint8_t(output[i])) - int(uint8_t(input[i]))) > 4 ? 1 : 0;
            }
            QVERIFY(changed > 50);
        }

        pass->removeTarget(t);
        v.free(upload);
        v.free(download);
        v.free(source);
        v.free(target);
        QVERIFY2(v.problems.isEmpty(), qPrintable(v.problems.join(QLatin1Char('\n'))));
    }

    void filtersAGameThroughTheLoader()
    {
        // Our layer above the validation layer, so it checks the layer's calls.
        QStringList layers{QStringLiteral("VK_LAYER_GFXTUNE_filters")};
        if (Vulkan::availableLayers().contains(QStringLiteral("VK_LAYER_KHRONOS_validation"))) {
            layers << QStringLiteral("VK_LAYER_KHRONOS_validation");
        }
        QVERIFY2(Vulkan::availableLayers().contains(QStringLiteral("VK_LAYER_GFXTUNE_filters")), "the build's layer manifest was not found");

        Values values = sample(2);
        values.sharpening = 40;
        writeSettings(m_settings, values);
        QFile::remove(m_report);

        Vulkan v;
        if (!v.create(layers, true)) {
            QSKIP(qPrintable(v.why));
        }
        // A plain 8-bit format the headless surface offers.
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(v.physical, v.surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(v.physical, v.surface, &formatCount, formats.data());
        std::optional<VkSurfaceFormatKHR> chosen;
        for (const VkSurfaceFormatKHR &sf : formats) {
            if ((sf.format == VK_FORMAT_B8G8R8A8_UNORM || sf.format == VK_FORMAT_R8G8B8A8_UNORM) && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = sf;
                break;
            }
        }
        if (!chosen) {
            QSKIP("the headless surface offers no 8-bit UNORM format");
        }

        auto info = layer::vkStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        info.surface = v.surface;
        // Room to hold every other image while one comes back (see below).
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(v.physical, v.surface, &caps);
        info.minImageCount = caps.minImageCount + 3;
        if (caps.maxImageCount != 0) {
            info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        }
        info.imageFormat = chosen->format;
        info.imageColorSpace = chosen->colorSpace;
        info.imageExtent = {Width, Height};
        info.imageArrayLayers = 1;
        // A game would not ask to read its own images; the layer adds that.
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        QCOMPARE(vkCreateSwapchainKHR(v.device, &info, nullptr, &swapchain), VK_SUCCESS);
        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(v.device, swapchain, &imageCount, nullptr);
        std::vector<VkImage> images(imageCount);
        vkGetSwapchainImagesKHR(v.device, swapchain, &imageCount, images.data());
        // Vulkan lets an application hold this many images at once, beyond
        // the one it is working on.
        const std::size_t maxHeld = imageCount - caps.minImageCount;

        // The layer reported what it is doing.
        QFile reportFile(m_report);
        QVERIFY(reportFile.open(QIODevice::ReadOnly));
        const QString reportText = QString::fromUtf8(reportFile.readAll());
        QVERIFY2(reportText.contains(QLatin1String("state=running")), qPrintable(reportText));
        QVERIFY2(reportText.contains(QStringLiteral("detail=%1×%2").arg(Width).arg(Height)), qPrintable(reportText));

        const std::vector<Rgb> picture = testPicture();
        const QByteArray input = pack(picture, chosen->format);
        Vulkan::Buffer upload = v.hostBuffer(VkDeviceSize(input.size()));
        Vulkan::Buffer download = v.hostBuffer(VkDeviceSize(input.size()));
        std::memcpy(upload.mapped, input.constData(), size_t(input.size()));
        const VkBufferImageCopy region = Vulkan::wholeImage();
        auto semaphoreInfo = layer::vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore rendered = VK_NULL_HANDLE;
        vkCreateSemaphore(v.device, &semaphoreInfo, nullptr, &rendered);

        // One frame as a game draws it: acquire, draw (here: copy the test
        // picture in), present. Then wait for that image to come back and
        // read what was shown.
        QString why;
        const auto presentFrame = [&]() -> QByteArray {
            why.clear();
            uint32_t index = 0;
            auto fenceInfo = layer::vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
            VkFence acquireFence = VK_NULL_HANDLE;
            vkCreateFence(v.device, &fenceInfo, nullptr, &acquireFence);
            if (const VkResult r = vkAcquireNextImageKHR(v.device, swapchain, UINT64_MAX, VK_NULL_HANDLE, acquireFence, &index); r != VK_SUCCESS) {
                why = QStringLiteral("first acquire: %1").arg(int(r));
                return {};
            }
            vkWaitForFences(v.device, 1, &acquireFence, VK_TRUE, UINT64_MAX);
            const bool drawn = v.run(
                [&](VkCommandBuffer cmd) {
                    v.barrier(cmd, images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    vkCmdCopyBufferToImage(cmd, upload.buffer, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    v.barrier(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                },
                rendered);
            auto present = layer::vkStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &rendered;
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &index;
            const VkResult presented = drawn ? vkQueuePresentKHR(v.queue, &present) : VK_ERROR_UNKNOWN;
            if (presented != VK_SUCCESS) {
                why = QStringLiteral("present: %1").arg(int(presented));
                vkDestroyFence(v.device, acquireFence, nullptr);
                return {};
            }
            vkQueueWaitIdle(v.queue);
            // A headless "screen" keeps the image it shows until the next
            // present and hands out the lowest free image: hold the others
            // (as many as Vulkan allows at once) and give them back, which
            // also frees the shown one, until the presented image returns.
            const auto giveBack = [&](uint32_t image, VkImageLayout layout) {
                v.run([&](VkCommandBuffer cmd) { v.barrier(cmd, images[image], layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR); }, rendered);
                auto back = present;
                back.pImageIndices = &image;
                vkQueuePresentKHR(v.queue, &back);
                vkQueueWaitIdle(v.queue);
            };
            std::vector<uint32_t> held;
            uint32_t again = UINT32_MAX;
            for (uint32_t tries = 0; tries < 4 * imageCount && again != index; ++tries) {
                if (held.size() >= maxHeld) {
                    for (const uint32_t image : held) {
                        giveBack(image, VK_IMAGE_LAYOUT_UNDEFINED);
                    }
                    held.clear();
                }
                vkResetFences(v.device, 1, &acquireFence);
                if (const VkResult r = vkAcquireNextImageKHR(v.device, swapchain, UINT64_MAX, VK_NULL_HANDLE, acquireFence, &again); r != VK_SUCCESS) {
                    why = QStringLiteral("acquire again: %1").arg(int(r));
                    break;
                }
                vkWaitForFences(v.device, 1, &acquireFence, VK_TRUE, UINT64_MAX);
                if (again != index) {
                    held.push_back(again);
                }
            }
            vkDestroyFence(v.device, acquireFence, nullptr);
            for (const uint32_t image : held) {
                giveBack(image, VK_IMAGE_LAYOUT_UNDEFINED);
            }
            if (again != index) {
                why += QStringLiteral(" image %1 did not come back").arg(index);
                return {};
            }
            v.run([&](VkCommandBuffer cmd) {
                v.barrier(cmd, images[index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vkCmdCopyImageToBuffer(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, download.buffer, 1, &region);
            });
            const QByteArray shownBytes(static_cast<const char *>(download.mapped), input.size());
            giveBack(index, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            return shownBytes;
        };

        // Filtered, as the settings file says.
        int worst = 0;
        QByteArray shown = presentFrame();
        QVERIFY2(!shown.isEmpty(), qPrintable(QStringLiteral("the frame did not come back: ") + why));
        QString mismatch = compare(shown, {reference(picture, params(values, chosen->format))}, chosen->format, 2, &worst);
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

        // Changed while the game runs: shows within the layer's half second.
        values = sample(3);
        writeSettings(m_settings, values);
        QTest::qWait(700);
        shown = presentFrame();
        QVERIFY2(!shown.isEmpty(), qPrintable(QStringLiteral("the second frame did not come back: ") + why));
        mismatch = compare(shown, {reference(picture, params(values, chosen->format))}, chosen->format, 2, &worst);
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

        // Switched off for comparing: the game's own picture, bit for bit.
        values.enabled = false;
        writeSettings(m_settings, values);
        QTest::qWait(700);
        shown = presentFrame();
        QVERIFY2(!shown.isEmpty(), qPrintable(QStringLiteral("the third frame did not come back: ") + why));
        QCOMPARE(shown, input);

        vkDestroySemaphore(v.device, rendered, nullptr);
        vkDestroySwapchainKHR(v.device, swapchain, nullptr);
        v.free(upload);
        v.free(download);
        QVERIFY2(v.problems.isEmpty(), qPrintable(v.problems.join(QLatin1Char('\n'))));
    }

    void loadsOnlyWhenGfxtuneRunAsks()
    {
        // As installed: an implicit layer, switched on by GFXTUNE_FILTERS=1.
        qunsetenv("VK_ADD_LAYER_PATH");
        qputenv("VK_ADD_IMPLICIT_LAYER_PATH", layerDir().toUtf8());
        Values values;
        values.sharpening = 50;
        writeSettings(m_settings, values);

        // Whether the layer took part: it reports on every swapchain.
        const auto layerRan = [this](QString *why) {
            QFile::remove(m_report);
            Vulkan v;
            if (!v.create({}, true)) {
                *why = v.why;
                return false;
            }
            auto info = layer::vkStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
            VkSurfaceCapabilitiesKHR caps{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(v.physical, v.surface, &caps);
            uint32_t count = 1;
            VkSurfaceFormatKHR format{};
            vkGetPhysicalDeviceSurfaceFormatsKHR(v.physical, v.surface, &count, &format);
            info.surface = v.surface;
            info.minImageCount = caps.minImageCount;
            info.imageFormat = format.format;
            info.imageColorSpace = format.colorSpace;
            info.imageExtent = {Width, Height};
            info.imageArrayLayers = 1;
            info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
            info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            if (vkCreateSwapchainKHR(v.device, &info, nullptr, &swapchain) != VK_SUCCESS) {
                *why = QStringLiteral("could not create a swapchain");
                return false;
            }
            vkDestroySwapchainKHR(v.device, swapchain, nullptr);
            return QFile::exists(m_report);
        };

        QString why;
        qunsetenv("GFXTUNE_FILTERS");
        const bool withoutRequest = layerRan(&why);
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        QVERIFY(!withoutRequest);

        qputenv("GFXTUNE_FILTERS", "1");
        QVERIFY2(layerRan(&why), qPrintable(why));

        // The user's way out, for any game.
        qputenv("DISABLE_GFXTUNE_FILTERS", "1");
        QVERIFY(!layerRan(&why));

        qunsetenv("DISABLE_GFXTUNE_FILTERS");
        qunsetenv("GFXTUNE_FILTERS");
        qunsetenv("VK_ADD_IMPLICIT_LAYER_PATH");
        qputenv("VK_ADD_LAYER_PATH", layerDir().toUtf8());
    }
};

QTEST_GUILESS_MAIN(LayerTest)
#include "tst_layer.moc"
