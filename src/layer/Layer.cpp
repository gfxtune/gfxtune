// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// gfxtune's Vulkan layer: draws the in-game filters into the game's picture
// just before it is shown. The game draws into a swapchain image as usual;
// when it presents that image, the layer copies it, draws the filtered copy
// back into the swapchain image and lets the present continue.
//
// It only loads where gfxtune-run sets GFXTUNE_FILTERS=1 (see the manifest),
// reads the game's settings from GFXTUNE_GLOBAL_SETTINGS and
// GFXTUNE_FILTERS_SETTINGS while the game runs, and writes what it did to
// GFXTUNE_FILTERS_REPORT for the game's page.
// Anything it cannot handle is passed through untouched.

#include "FilterPass.h"
#include "LayerSettings.h"
#include "Pacing.h"
#include "Recorder.h"
#include "Smaa.h"
#include "VulkanFunctions.h"

#include <vulkan/vk_layer.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define GFXTUNE_EXPORT extern "C" __attribute__((visibility("default")))

namespace gfxtune::layer
{

namespace
{

// Dispatchable handles start with a pointer to the loader's dispatch table;
// an instance and its physical devices share one, as do a device and its
// queues and command buffers.
void *dispatchKey(const void *handle)
{
    return *static_cast<void *const *>(handle);
}

struct InstanceData {
    VkInstance instance = VK_NULL_HANDLE;
    InstanceFunctions vk;
};

struct ImageResources {
    VkImage swapchainImage = VK_NULL_HANDLE;
    OwnImage copy; // what the game drew, read by the filters
    FilterPass::Target target;
    // With anti-aliasing: SMAA reads the copy, the last step its result.
    Smaa::Source smaaSource;
    FilterPass::Target smaaTarget;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t cmdFamily = 0;
    VkSemaphore done = VK_NULL_HANDLE; // the present waits on this
    VkFence fence = VK_NULL_HANDLE;    // the last filter submit for this image
};

struct SwapchainData {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    bool linear = false; // an sRGB format: the shader reads linear light
    // The swapchain was made so the filters can read it. Their images are
    // only made once a filter is on: measuring alone needs none.
    bool filterable = false;
    bool ready = false;
    bool reported = false;
    std::string skipped; // why it cannot be filtered
    FilterPass *pass = nullptr;
    std::vector<ImageResources> images;
    // SMAA's work images, shared by the images; made when it is first on.
    Smaa *smaa = nullptr;
    Smaa::Frame smaaFrame;
    bool smaaReady = false;
    bool smaaFailed = false;
    // The queue the last frame was filtered on: the shared work images are
    // only safe to reuse in order on one queue.
    VkQueue lastQueue = VK_NULL_HANDLE;
};

struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    InstanceData *instance = nullptr;
    DeviceFunctions vk;
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    std::vector<VkQueueFamilyProperties> families;
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    float maxLodBias = 0;

    std::mutex mutex;
    std::unordered_map<VkQueue, uint32_t> queueFamilies;
    std::unordered_map<uint32_t, VkCommandPool> pools;
    std::unordered_map<VkSwapchainKHR, std::unique_ptr<SwapchainData>> swapchains;
    std::unordered_map<int, std::unique_ptr<FilterPass>> passes; // by VkFormat
    std::unordered_map<int, std::unique_ptr<Smaa>> smaa;         // by VkFormat
};

std::mutex g_mutex;
std::unordered_map<void *, std::unique_ptr<InstanceData>> g_instances;
std::unordered_map<void *, std::unique_ptr<DeviceData>> g_devices;
// The game's main window: the biggest swapchain. Only its frames are paced
// and measured; a launcher or a second window must not count.
VkSwapchainKHR g_primary = VK_NULL_HANDLE;
std::uint64_t g_primaryArea = 0;
std::shared_ptr<PerformanceRecorder> g_recorder;

std::mutex g_limiterMutex;
FrameLimiter g_limiter;

InstanceData *instanceData(const void *handle)
{
    const std::scoped_lock lock(g_mutex);
    const auto found = g_instances.find(dispatchKey(handle));
    return found != g_instances.end() ? found->second.get() : nullptr;
}

DeviceData *deviceData(const void *handle)
{
    const std::scoped_lock lock(g_mutex);
    const auto found = g_devices.find(dispatchKey(handle));
    return found != g_devices.end() ? found->second.get() : nullptr;
}

std::string environment(const char *name)
{
    // Read while the game starts its instance; games do not change their
    // environment while Vulkan is in use.
    const char *value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
    return value ? std::string(value) : std::string();
}

SettingsFile &settings()
{
    // The global settings when the game follows them, then its own.
    static SettingsFile file({environment("GFXTUNE_GLOBAL_SETTINGS"), environment("GFXTUNE_FILTERS_SETTINGS")});
    return file;
}

void report(bool running, const std::string &detail)
{
    static const std::string path = environment("GFXTUNE_FILTERS_REPORT");
    writeReport(path, running, detail);
}

// Made on first use when gfxtune-run asked for measuring; stopped with the
// last Vulkan instance, before the loader unloads the layer.
std::shared_ptr<PerformanceRecorder> recorder()
{
    static const std::string dir = environment("GFXTUNE_PERF_DIR");
    if (dir.empty()) {
        return nullptr;
    }
    const std::scoped_lock lock(g_mutex);
    if (!g_recorder) {
        const std::string root = environment("GFXTUNE_SYSFS_ROOT");
        g_recorder = std::make_shared<PerformanceRecorder>(dir, root.empty() ? std::string("/sys") : root);
    }
    return g_recorder;
}

bool presentsPrimary(const VkPresentInfoKHR &info)
{
    const std::scoped_lock lock(g_mutex);
    for (uint32_t i = 0; i < info.swapchainCount; ++i) {
        if (info.pSwapchains[i] == g_primary) {
            return true;
        }
    }
    return false;
}

// ---- Per-swapchain resources ----

void destroyImage(DeviceData &d, const SwapchainData &swapchain, ImageResources &image)
{
    if (image.fence != VK_NULL_HANDLE) {
        d.vk.WaitForFences(d.device, 1, &image.fence, VK_TRUE, UINT64_MAX);
    }
    if (image.cmd != VK_NULL_HANDLE) {
        d.vk.FreeCommandBuffers(d.device, d.pools[image.cmdFamily], 1, &image.cmd);
    }
    if (swapchain.pass) {
        swapchain.pass->removeTarget(image.target);
        swapchain.pass->removeTarget(image.smaaTarget);
    }
    if (swapchain.smaa) {
        swapchain.smaa->destroySource(image.smaaSource);
    }
    d.vk.DestroyFence(d.device, image.fence, nullptr);
    d.vk.DestroySemaphore(d.device, image.done, nullptr);
    image.copy.destroy(d.vk, d.device);
    image = ImageResources{};
}

void destroySwapchainData(DeviceData &d, SwapchainData &swapchain)
{
    for (ImageResources &image : swapchain.images) {
        destroyImage(d, swapchain, image);
    }
    swapchain.images.clear();
    if (swapchain.smaa) {
        swapchain.smaa->destroyFrame(swapchain.smaaFrame);
    }
    swapchain.smaaReady = false;
}

bool createImage(const DeviceData &d, const SwapchainData &swapchain, VkImage swapchainImage, ImageResources &image, std::string *error)
{
    image.swapchainImage = swapchainImage;

    // sRGB copies also get a plain view, for SMAA's edge detection.
    if (!image.copy.create(d.vk, d.device, d.memory, swapchain.format, swapchain.extent,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, swapchain.linear, error)) {
        return false;
    }
    if (!swapchain.pass->addTarget(image.copy.image, swapchainImage, swapchain.extent, image.target, error)) {
        return false;
    }
    const auto semaphore = vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    auto fence = vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (d.vk.CreateSemaphore(d.device, &semaphore, nullptr, &image.done) != VK_SUCCESS
        || d.vk.CreateFence(d.device, &fence, nullptr, &image.fence) != VK_SUCCESS) {
        *error = "could not create synchronisation objects";
        return false;
    }
    return true;
}

// Checks everything the filters need from the device, the surface and the
// format, and says why not otherwise.
bool canFilter(const DeviceData &d, const VkSwapchainCreateInfoKHR &info, std::string *reason)
{
    if (!d.vk.complete() || !d.setLoaderData || !d.instance || !d.instance->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR) {
        *reason = "the Vulkan driver lacks functions the filters need";
        return false;
    }
    if (!supportedFormat(info.imageFormat, info.imageColorSpace, reason)) {
        return false;
    }
    if (info.imageArrayLayers != 1 || info.presentMode == VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR
        || info.presentMode == VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR) {
        *reason = "the game uses a kind of swapchain the filters do not handle";
        return false;
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (d.instance->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(d.physicalDevice, info.surface, &caps) != VK_SUCCESS
        || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        *reason = "the display does not let the filters read the game's picture";
        return false;
    }
    VkFormatProperties format{};
    d.instance->vk.GetPhysicalDeviceFormatProperties(d.physicalDevice, info.imageFormat, &format);
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if ((format.optimalTilingFeatures & needed) != needed) {
        *reason = "the graphics card cannot filter the game's picture format";
        return false;
    }
    return true;
}

// Makes the images the filters need for one swapchain.
bool prepareFilters(DeviceData &d, VkSwapchainKHR swapchain, SwapchainData &data, std::string *error)
{
    auto &pass = d.passes[int(data.format)];
    if (!pass) {
        pass = FilterPass::create(d.vk, d.device, data.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, error);
        if (!pass) {
            return false;
        }
    }
    data.pass = pass.get();

    uint32_t count = 0;
    if (d.vk.GetSwapchainImagesKHR(d.device, swapchain, &count, nullptr) != VK_SUCCESS) {
        *error = "could not list the swapchain's images";
        return false;
    }
    std::vector<VkImage> images(count);
    if (d.vk.GetSwapchainImagesKHR(d.device, swapchain, &count, images.data()) != VK_SUCCESS) {
        *error = "could not list the swapchain's images";
        return false;
    }
    data.images.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!createImage(d, data, images[i], data.images[i], error)) {
            destroySwapchainData(d, data);
            return false;
        }
    }
    return true;
}

// The first time a filter is on for a swapchain: make its images, or say
// once why it cannot be filtered.
void ensureFilters(DeviceData &d, VkSwapchainKHR swapchain, SwapchainData &data)
{
    if (data.ready || data.reported) {
        return;
    }
    if (data.filterable && prepareFilters(d, swapchain, data, &data.skipped)) {
        data.ready = true;
        report(true, std::to_string(data.extent.width) + "×" + std::to_string(data.extent.height) + ", " + formatName(data.format));
    } else {
        data.filterable = false;
        report(false, data.skipped);
    }
    data.reported = true;
}

// The first time anti-aliasing is on for a swapchain: SMAA's work images,
// and for each image a way in and a last step reading SMAA's result. If
// that fails, the other filters go on without it.
void ensureSmaa(DeviceData &d, SwapchainData &swapchain)
{
    if (swapchain.smaaReady || swapchain.smaaFailed || !swapchain.ready) {
        return;
    }
    std::string error;
    auto &smaa = d.smaa[int(swapchain.format)];
    if (!smaa) {
        smaa = Smaa::create(d.vk, d.device, d.memory, swapchain.format, &error);
    }
    swapchain.smaa = smaa.get();
    bool ok = smaa && smaa->createFrame(swapchain.extent, swapchain.smaaFrame, &error);
    for (ImageResources &image : swapchain.images) {
        ok = ok && smaa->createSource(swapchain.smaaFrame, image.copy.image, image.smaaSource, &error)
            && swapchain.pass->addTarget(swapchain.smaaFrame.output.image, image.swapchainImage, swapchain.extent, image.smaaTarget,
                                         &error);
    }
    if (!ok) {
        for (ImageResources &image : swapchain.images) {
            swapchain.pass->removeTarget(image.smaaTarget);
            if (smaa) {
                smaa->destroySource(image.smaaSource);
            }
        }
        if (smaa) {
            smaa->destroyFrame(swapchain.smaaFrame);
        }
        swapchain.smaaFailed = true;
        report(true,
               std::to_string(swapchain.extent.width) + "×" + std::to_string(swapchain.extent.height) + ", " + formatName(swapchain.format)
                   + "; anti-aliasing could not start: " + error);
        return;
    }
    swapchain.smaaReady = true;
}

// ---- Recording ----

VkCommandBuffer commandBuffer(DeviceData &d, ImageResources &image, uint32_t family)
{
    if (image.cmd != VK_NULL_HANDLE && image.cmdFamily == family) {
        return image.cmd;
    }
    if (image.cmd != VK_NULL_HANDLE) {
        d.vk.FreeCommandBuffers(d.device, d.pools[image.cmdFamily], 1, &image.cmd);
        image.cmd = VK_NULL_HANDLE;
    }
    VkCommandPool &pool = d.pools[family];
    if (pool == VK_NULL_HANDLE) {
        auto info = vkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = family;
        if (d.vk.CreateCommandPool(d.device, &info, nullptr, &pool) != VK_SUCCESS) {
            pool = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
    }
    auto allocate = vkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (d.vk.AllocateCommandBuffers(d.device, &allocate, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    // Command buffers are dispatchable: the loader has to know this one.
    if (d.setLoaderData(d.device, cmd) != VK_SUCCESS) {
        d.vk.FreeCommandBuffers(d.device, pool, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    image.cmd = cmd;
    image.cmdFamily = family;
    return cmd;
}

void recordFilter(const DeviceData &d, const SwapchainData &swapchain, const ImageResources &image, const filters::ShaderParams &params,
                  bool antiAlias)
{
    VkCommandBuffer cmd = image.cmd;
    auto begin = vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d.vk.BeginCommandBuffer(cmd, &begin);

    const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier before[2]{};
    // The game's finished picture, made visible by the semaphores the present
    // waited on (now waited on by this submit).
    before[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    before[0].srcAccessMask = 0;
    before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    before[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = image.swapchainImage;
    before[0].subresourceRange = whole;
    before[1] = before[0];
    before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1].image = image.copy.image;
    d.vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, before);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = region.srcSubresource;
    region.extent = {swapchain.extent.width, swapchain.extent.height, 1};
    d.vk.CmdCopyImage(cmd, image.swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.copy.image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier between[2]{};
    between[0] = before[1];
    between[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    between[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    between[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    between[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    between[1] = before[0];
    between[1].srcAccessMask = 0; // only reads happened: an execution dependency is enough
    between[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    between[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    between[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    d.vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                            nullptr, 2, between);

    if (antiAlias) {
        swapchain.smaa->record(cmd, swapchain.smaaFrame, image.smaaSource);
        swapchain.pass->record(cmd, image.smaaTarget, params);
    } else {
        swapchain.pass->record(cmd, image.target, params); // leaves the image ready to present
    }
    d.vk.EndCommandBuffer(cmd);
}

// ---- Hooks ----

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator,
                                              VkInstance *instance)
{
    auto *link = static_cast<VkLayerInstanceCreateInfo *>(const_cast<void *>(info->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO)) {
        link = static_cast<VkLayerInstanceCreateInfo *>(const_cast<void *>(link->pNext));
    }
    if (!link || !link->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext; // the next layer's turn
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = create(info, allocator, instance);
    if (result != VK_SUCCESS) {
        return result;
    }
    auto data = std::make_unique<InstanceData>();
    data->instance = *instance;
    data->vk.load(*instance, next);
    const std::scoped_lock lock(g_mutex);
    g_instances[dispatchKey(*instance)] = std::move(data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    if (!instance) {
        return;
    }
    std::unique_ptr<InstanceData> data;
    std::shared_ptr<PerformanceRecorder> lastRecorder;
    {
        const std::scoped_lock lock(g_mutex);
        const auto found = g_instances.find(dispatchKey(instance));
        if (found == g_instances.end()) {
            return;
        }
        data = std::move(found->second);
        g_instances.erase(found);
        // The loader may unload the layer now: its thread has to go first.
        if (g_instances.empty()) {
            lastRecorder = std::move(g_recorder);
        }
    }
    lastRecorder.reset();
    data->vk.DestroyInstance(instance, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *info,
                                            const VkAllocationCallbacks *allocator, VkDevice *device)
{
    InstanceData *instance = instanceData(physicalDevice);
    VkLayerDeviceCreateInfo *link = nullptr;
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    for (auto *item = static_cast<VkLayerDeviceCreateInfo *>(const_cast<void *>(info->pNext)); item;
         item = static_cast<VkLayerDeviceCreateInfo *>(const_cast<void *>(item->pNext))) {
        if (item->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            continue;
        }
        if (item->function == VK_LAYER_LINK_INFO && !link) {
            link = item;
        } else if (item->function == VK_LOADER_DATA_CALLBACK) {
            setLoaderData = item->u.pfnSetDeviceLoaderData;
        }
    }
    if (!instance || !link || !link->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr nextInstance = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr nextDevice = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const auto create = reinterpret_cast<PFN_vkCreateDevice>(nextInstance(instance->instance, "vkCreateDevice"));
    if (!create) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = create(physicalDevice, info, allocator, device);
    if (result != VK_SUCCESS) {
        return result;
    }

    auto data = std::make_unique<DeviceData>();
    data->device = *device;
    data->physicalDevice = physicalDevice;
    data->instance = instance;
    data->setLoaderData = setLoaderData;
    data->vk.load(*device, nextDevice);
    uint32_t familyCount = 0;
    instance->vk.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    data->families.resize(familyCount);
    instance->vk.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, data->families.data());
    instance->vk.GetPhysicalDeviceMemoryProperties(physicalDevice, &data->memory);
    VkPhysicalDeviceProperties properties{};
    instance->vk.GetPhysicalDeviceProperties(physicalDevice, &properties);
    data->vendorId = properties.vendorID;
    data->deviceId = properties.deviceID;
    data->maxLodBias = properties.limits.maxSamplerLodBias;

    const std::scoped_lock lock(g_mutex);
    g_devices[dispatchKey(*device)] = std::move(data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *allocator)
{
    if (!device) {
        return;
    }
    std::unique_ptr<DeviceData> data;
    {
        const std::scoped_lock lock(g_mutex);
        const auto found = g_devices.find(dispatchKey(device));
        if (found == g_devices.end()) {
            return;
        }
        data = std::move(found->second);
        g_devices.erase(found);
    }
    // Swapchains belong to the device and should be gone; clean up anyway.
    for (auto &[handle, swapchain] : data->swapchains) {
        destroySwapchainData(*data, *swapchain);
    }
    data->swapchains.clear();
    data->passes.clear();
    data->smaa.clear();
    for (auto &[family, pool] : data->pools) {
        data->vk.DestroyCommandPool(device, pool, nullptr);
    }
    data->vk.DestroyDevice(device, allocator);
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue *queue)
{
    DeviceData *d = deviceData(device);
    d->vk.GetDeviceQueue(device, family, index, queue);
    if (*queue) {
        const std::scoped_lock lock(d->mutex);
        d->queueFamilies[*queue] = family;
    }
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *queue)
{
    DeviceData *d = deviceData(device);
    d->vk.GetDeviceQueue2(device, info, queue);
    if (*queue) {
        const std::scoped_lock lock(d->mutex);
        d->queueFamilies[*queue] = info->queueFamilyIndex;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                  const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
    DeviceData *d = deviceData(device);
    const std::scoped_lock lock(d->mutex);

    auto data = std::make_unique<SwapchainData>();
    data->format = info->imageFormat;
    data->extent = info->imageExtent;
    data->linear = isSrgbFormat(info->imageFormat);
    data->filterable = canFilter(*d, *info, &data->skipped);

    VkSwapchainCreateInfoKHR withCopy = *info;
    // Reading the game's picture needs one more usage; drawing into it is
    // what every game already asks for.
    withCopy.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkResult result = d->vk.CreateSwapchainKHR(device, data->filterable ? &withCopy : info, allocator, swapchain);
    if (result != VK_SUCCESS && data->filterable) {
        data->filterable = false;
        data->skipped = "the driver refused a swapchain the filters can read";
        result = d->vk.CreateSwapchainKHR(device, info, allocator, swapchain);
    }
    if (result != VK_SUCCESS) {
        return result;
    }

    // A filter on from the start: ready before the first frame.
    if (settings().config().filters.active()) {
        ensureFilters(*d, *swapchain, *data);
    }

    const std::uint64_t area = std::uint64_t(info->imageExtent.width) * info->imageExtent.height;
    bool primary = false;
    {
        const std::scoped_lock globalLock(g_mutex);
        if (g_primary == VK_NULL_HANDLE || area >= g_primaryArea) {
            g_primary = *swapchain;
            g_primaryArea = area;
            primary = true;
        }
    }
    if (primary) {
        if (const auto r = recorder()) {
            r->setDevice(d->vendorId, d->deviceId);
        }
    }
    d->swapchains[*swapchain] = std::move(data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator)
{
    DeviceData *d = deviceData(device);
    {
        const std::scoped_lock lock(d->mutex);
        const auto found = d->swapchains.find(swapchain);
        if (found != d->swapchains.end()) {
            destroySwapchainData(*d, *found->second);
            d->swapchains.erase(found);
        }
    }
    {
        const std::scoped_lock globalLock(g_mutex);
        if (g_primary == swapchain) {
            g_primary = VK_NULL_HANDLE;
            g_primaryArea = 0;
        }
    }
    d->vk.DestroySwapchainKHR(device, swapchain, allocator);
}

// Draws the filters into every swapchain image being presented that can
// have them, and collects the semaphores the present then waits on. Returns
// an error only when nothing was queued and the game's wait is still open.
VkResult drawFilters(DeviceData &d, VkQueue queue, const VkPresentInfoKHR &info, const filters::Values &values,
                     std::vector<VkSemaphore> &drawn)
{
    const std::scoped_lock lock(d.mutex);
    const auto family = d.queueFamilies.find(queue);
    if (family == d.queueFamilies.end() || !(d.families[family->second].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        return VK_SUCCESS;
    }

    bool waitedForGame = false;
    for (uint32_t i = 0; i < info.swapchainCount; ++i) {
        const auto found = d.swapchains.find(info.pSwapchains[i]);
        if (found == d.swapchains.end()) {
            continue;
        }
        SwapchainData &swapchain = *found->second;
        ensureFilters(d, info.pSwapchains[i], swapchain);
        if (!swapchain.ready || info.pImageIndices[i] >= swapchain.images.size()) {
            continue;
        }
        const bool antiAlias = values.antiAliasing != filters::AntiAliasing.neutral;
        if (antiAlias) {
            ensureSmaa(d, swapchain);
        }
        const bool smaa = antiAlias && swapchain.smaaReady;
        // On another queue than last time, the shared work images may still
        // be in use there: wait for this swapchain's earlier draws.
        if (smaa && swapchain.lastQueue != VK_NULL_HANDLE && swapchain.lastQueue != queue) {
            for (const ImageResources &other : swapchain.images) {
                d.vk.WaitForFences(d.device, 1, &other.fence, VK_TRUE, 1'000'000'000);
            }
        }
        ImageResources &image = swapchain.images[info.pImageIndices[i]];
        // The last filter draw into this image is long done by the time the
        // game presents it again; wait briefly in case it is not.
        if (d.vk.WaitForFences(d.device, 1, &image.fence, VK_TRUE, 1'000'000'000) != VK_SUCCESS
            || !commandBuffer(d, image, family->second)) {
            continue;
        }
        d.vk.ResetFences(d.device, 1, &image.fence);
        recordFilter(d, swapchain, image, filters::shaderParams(values, swapchain.linear), smaa);

        // The first submit takes over the game's wait; later ones follow it
        // in queue order and through the all-commands barrier.
        std::vector<VkPipelineStageFlags> stages(waitedForGame ? 0 : info.waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto submit = vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = waitedForGame ? 0 : info.waitSemaphoreCount;
        submit.pWaitSemaphores = info.pWaitSemaphores;
        submit.pWaitDstStageMask = stages.data();
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &image.cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &image.done;
        const VkResult result = d.vk.QueueSubmit(queue, 1, &submit, image.fence);
        if (result != VK_SUCCESS) {
            // Nothing was queued: the fence stays unsignalled, so make a new one.
            d.vk.DestroyFence(d.device, image.fence, nullptr);
            auto fence = vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            d.vk.CreateFence(d.device, &fence, nullptr, &image.fence);
            if (waitedForGame) {
                continue;
            }
            return result;
        }
        waitedForGame = true;
        drawn.push_back(image.done);
        swapchain.lastQueue = queue;
        if (smaa) {
            swapchain.smaa->uploadSubmitted();
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
    DeviceData *d = deviceData(queue);
    const filters::LayerConfig config = settings().config();

    VkPresentInfoKHR filtered = *info;
    std::vector<VkSemaphore> drawn;
    if (config.filters.active()) {
        const VkResult result = drawFilters(*d, queue, *info, config.filters, drawn);
        if (result != VK_SUCCESS) {
            return result;
        }
        if (!drawn.empty()) {
            filtered.waitSemaphoreCount = uint32_t(drawn.size());
            filtered.pWaitSemaphores = drawn.data();
        }
    }

    // Pacing and measuring follow the game's main window only. The GPU
    // works on the filters meanwhile.
    if (presentsPrimary(*info)) {
        FrameLimiter::Clock::time_point when;
        {
            const std::scoped_lock lock(g_limiterMutex);
            when = g_limiter.slot(config.frameCap, FrameLimiter::Clock::now());
        }
        FrameLimiter::waitUntil(when);
        if (const auto r = recorder()) {
            r->frame(FrameLimiter::Clock::now());
        }
    }
    return d->vk.QueuePresentKHR(queue, &filtered);
}

// Texture detail: the LOD bias added to every sampler the game makes from
// now on, within what the card allows.
VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice device, const VkSamplerCreateInfo *info, const VkAllocationCallbacks *allocator,
                                             VkSampler *sampler)
{
    const DeviceData *d = deviceData(device);
    const int bias = settings().config().lodBias;
    if (bias == 0) {
        return d->vk.CreateSampler(device, info, allocator, sampler);
    }
    VkSamplerCreateInfo biased = *info;
    biased.mipLodBias = biasedLod(info->mipLodBias, bias, d->maxLodBias);
    return d->vk.CreateSampler(device, &biased, allocator, sampler);
}

// ---- Lookup ----

PFN_vkVoidFunction deviceHook(const char *name)
{
#define GFXTUNE_HOOK(fn)                                                                                                                   \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                                                \
        return reinterpret_cast<PFN_vkVoidFunction>(&(fn));                                                                                \
    }
    GFXTUNE_HOOK(DestroyDevice)
    GFXTUNE_HOOK(GetDeviceQueue)
    GFXTUNE_HOOK(GetDeviceQueue2)
    GFXTUNE_HOOK(CreateSwapchainKHR)
    GFXTUNE_HOOK(DestroySwapchainKHR)
    GFXTUNE_HOOK(QueuePresentKHR)
    GFXTUNE_HOOK(CreateSampler)
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char *name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char *name)
{
    GFXTUNE_HOOK(GetInstanceProcAddr)
    GFXTUNE_HOOK(CreateInstance)
    GFXTUNE_HOOK(DestroyInstance)
    GFXTUNE_HOOK(CreateDevice)
    GFXTUNE_HOOK(GetDeviceProcAddr)
    if (!instance) {
        return nullptr;
    }
    const InstanceData *data = instanceData(instance);
    if (!data) {
        return nullptr;
    }
    const PFN_vkVoidFunction next = data->vk.GetInstanceProcAddr(instance, name);
    // Only stand in for functions the layers below provide.
    if (next) {
        if (const PFN_vkVoidFunction hook = deviceHook(name)) {
            return hook;
        }
    }
    return next;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char *name)
{
    GFXTUNE_HOOK(GetDeviceProcAddr)
    const DeviceData *data = device ? deviceData(device) : nullptr;
    if (!data) {
        return nullptr;
    }
    const PFN_vkVoidFunction next = data->vk.GetDeviceProcAddr(device, name);
    if (next) {
        if (const PFN_vkVoidFunction hook = deviceHook(name)) {
            return hook;
        }
    }
    return next;
}

#undef GFXTUNE_HOOK

} // namespace

} // namespace gfxtune::layer

GFXTUNE_EXPORT VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version)
{
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || version->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = gfxtune::layer::GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = gfxtune::layer::GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
