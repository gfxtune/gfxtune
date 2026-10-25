// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// gfxtune's Vulkan layer: draws the in-game filters into the game's picture
// just before it is shown. The game draws into a swapchain image as usual;
// when it presents that image, the layer copies it, draws the filtered copy
// back into the swapchain image and lets the present continue.
//
// It only loads where gfxtune-run sets GFXTUNE_FILTERS=1 (see the manifest),
// reads the game's settings file from GFXTUNE_FILTERS_SETTINGS while the game
// runs, and writes what it did to GFXTUNE_FILTERS_REPORT for the game's page.
// Anything it cannot handle is passed through untouched.

#include "FilterPass.h"
#include "LayerSettings.h"
#include "VulkanFunctions.h"

#include <vulkan/vk_layer.h>

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
    VkImage copy = VK_NULL_HANDLE; // what the game drew, read by the shader
    VkDeviceMemory memory = VK_NULL_HANDLE;
    FilterPass::Target target;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t cmdFamily = 0;
    VkSemaphore done = VK_NULL_HANDLE; // the present waits on this
    VkFence fence = VK_NULL_HANDLE;    // the last filter submit for this image
};

struct SwapchainData {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    bool linear = false; // an sRGB format: the shader reads linear light
    FilterPass *pass = nullptr;
    std::vector<ImageResources> images;
};

struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    InstanceData *instance = nullptr;
    DeviceFunctions vk;
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    std::vector<VkQueueFamilyProperties> families;
    VkPhysicalDeviceMemoryProperties memory{};

    std::mutex mutex;
    std::unordered_map<VkQueue, uint32_t> queueFamilies;
    std::unordered_map<uint32_t, VkCommandPool> pools;
    std::unordered_map<VkSwapchainKHR, std::unique_ptr<SwapchainData>> swapchains;
    std::unordered_map<int, std::unique_ptr<FilterPass>> passes; // by VkFormat
};

std::mutex g_mutex;
std::unordered_map<void *, std::unique_ptr<InstanceData>> g_instances;
std::unordered_map<void *, std::unique_ptr<DeviceData>> g_devices;

InstanceData *instanceData(const void *handle)
{
    std::lock_guard lock(g_mutex);
    const auto found = g_instances.find(dispatchKey(handle));
    return found != g_instances.end() ? found->second.get() : nullptr;
}

DeviceData *deviceData(const void *handle)
{
    std::lock_guard lock(g_mutex);
    const auto found = g_devices.find(dispatchKey(handle));
    return found != g_devices.end() ? found->second.get() : nullptr;
}

std::string environment(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

SettingsFile &settings()
{
    static SettingsFile file(environment("GFXTUNE_FILTERS_SETTINGS"));
    return file;
}

void report(bool running, const std::string &detail)
{
    static const std::string path = environment("GFXTUNE_FILTERS_REPORT");
    writeReport(path, running, detail);
}

// ---- Per-swapchain resources ----

bool memoryType(const DeviceData &d, uint32_t allowed, uint32_t &out)
{
    for (const VkMemoryPropertyFlags wanted : {VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), VkMemoryPropertyFlags(0)}) {
        for (uint32_t i = 0; i < d.memory.memoryTypeCount; ++i) {
            if ((allowed & (1u << i)) && (d.memory.memoryTypes[i].propertyFlags & wanted) == wanted) {
                out = i;
                return true;
            }
        }
    }
    return false;
}

void destroyImage(DeviceData &d, SwapchainData &swapchain, ImageResources &image)
{
    if (image.fence != VK_NULL_HANDLE) {
        d.vk.WaitForFences(d.device, 1, &image.fence, VK_TRUE, UINT64_MAX);
    }
    if (image.cmd != VK_NULL_HANDLE) {
        d.vk.FreeCommandBuffers(d.device, d.pools[image.cmdFamily], 1, &image.cmd);
    }
    if (swapchain.pass) {
        swapchain.pass->removeTarget(image.target);
    }
    d.vk.DestroyFence(d.device, image.fence, nullptr);
    d.vk.DestroySemaphore(d.device, image.done, nullptr);
    d.vk.DestroyImage(d.device, image.copy, nullptr);
    d.vk.FreeMemory(d.device, image.memory, nullptr);
    image = ImageResources{};
}

void destroySwapchainData(DeviceData &d, SwapchainData &swapchain)
{
    for (ImageResources &image : swapchain.images) {
        destroyImage(d, swapchain, image);
    }
    swapchain.images.clear();
}

bool createImage(DeviceData &d, SwapchainData &swapchain, VkImage swapchainImage, ImageResources &image, std::string *error)
{
    image.swapchainImage = swapchainImage;

    auto info = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = swapchain.format;
    info.extent = {swapchain.extent.width, swapchain.extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (d.vk.CreateImage(d.device, &info, nullptr, &image.copy) != VK_SUCCESS) {
        *error = "could not create an image for the copy";
        return false;
    }
    VkMemoryRequirements needs{};
    d.vk.GetImageMemoryRequirements(d.device, image.copy, &needs);
    auto allocate = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocate.allocationSize = needs.size;
    if (!memoryType(d, needs.memoryTypeBits, allocate.memoryTypeIndex)
        || d.vk.AllocateMemory(d.device, &allocate, nullptr, &image.memory) != VK_SUCCESS
        || d.vk.BindImageMemory(d.device, image.copy, image.memory, 0) != VK_SUCCESS) {
        *error = "not enough video memory for the copy";
        return false;
    }
    if (!swapchain.pass->addTarget(image.copy, swapchainImage, swapchain.extent, image.target, error)) {
        return false;
    }
    auto semaphore = vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
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
bool canFilter(DeviceData &d, const VkSwapchainCreateInfoKHR &info, std::string *reason)
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

std::unique_ptr<SwapchainData> createSwapchainData(DeviceData &d, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR &info,
                                                   std::string *error)
{
    auto data = std::make_unique<SwapchainData>();
    data->format = info.imageFormat;
    data->extent = info.imageExtent;
    data->linear = isSrgbFormat(info.imageFormat);

    auto &pass = d.passes[int(info.imageFormat)];
    if (!pass) {
        pass = FilterPass::create(d.vk, d.device, info.imageFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, error);
        if (!pass) {
            return nullptr;
        }
    }
    data->pass = pass.get();

    uint32_t count = 0;
    if (d.vk.GetSwapchainImagesKHR(d.device, swapchain, &count, nullptr) != VK_SUCCESS) {
        *error = "could not list the swapchain's images";
        return nullptr;
    }
    std::vector<VkImage> images(count);
    if (d.vk.GetSwapchainImagesKHR(d.device, swapchain, &count, images.data()) != VK_SUCCESS) {
        *error = "could not list the swapchain's images";
        return nullptr;
    }
    data->images.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!createImage(d, *data, images[i], data->images[i], error)) {
            destroySwapchainData(d, *data);
            return nullptr;
        }
    }
    return data;
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

void recordFilter(const DeviceData &d, const SwapchainData &swapchain, const ImageResources &image, const filters::ShaderParams &params)
{
    const VkCommandBuffer cmd = image.cmd;
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
    before[1].image = image.copy;
    d.vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, before);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = region.srcSubresource;
    region.extent = {swapchain.extent.width, swapchain.extent.height, 1};
    d.vk.CmdCopyImage(cmd, image.swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.copy,
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

    swapchain.pass->record(cmd, image.target, params); // leaves the image ready to present
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
    std::lock_guard lock(g_mutex);
    g_instances[dispatchKey(*instance)] = std::move(data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    if (!instance) {
        return;
    }
    std::unique_ptr<InstanceData> data;
    {
        std::lock_guard lock(g_mutex);
        const auto found = g_instances.find(dispatchKey(instance));
        if (found == g_instances.end()) {
            return;
        }
        data = std::move(found->second);
        g_instances.erase(found);
    }
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

    std::lock_guard lock(g_mutex);
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
        std::lock_guard lock(g_mutex);
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
        std::lock_guard lock(d->mutex);
        d->queueFamilies[*queue] = family;
    }
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *queue)
{
    DeviceData *d = deviceData(device);
    d->vk.GetDeviceQueue2(device, info, queue);
    if (*queue) {
        std::lock_guard lock(d->mutex);
        d->queueFamilies[*queue] = info->queueFamilyIndex;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                  const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
    DeviceData *d = deviceData(device);
    std::lock_guard lock(d->mutex);

    std::string reason;
    bool filter = canFilter(*d, *info, &reason);
    VkSwapchainCreateInfoKHR withCopy = *info;
    // Reading the game's picture needs one more usage; drawing into it is
    // what every game already asks for.
    withCopy.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkResult result = d->vk.CreateSwapchainKHR(device, filter ? &withCopy : info, allocator, swapchain);
    if (result != VK_SUCCESS && filter) {
        filter = false;
        reason = "the driver refused a swapchain the filters can read";
        result = d->vk.CreateSwapchainKHR(device, info, allocator, swapchain);
    }
    if (result != VK_SUCCESS) {
        return result;
    }
    if (filter) {
        std::unique_ptr<SwapchainData> data = createSwapchainData(*d, *swapchain, *info, &reason);
        if (data) {
            report(true, std::to_string(info->imageExtent.width) + "×" + std::to_string(info->imageExtent.height) + ", "
                             + formatName(info->imageFormat));
            d->swapchains[*swapchain] = std::move(data);
            return result;
        }
    }
    report(false, reason);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator)
{
    DeviceData *d = deviceData(device);
    {
        std::lock_guard lock(d->mutex);
        const auto found = d->swapchains.find(swapchain);
        if (found != d->swapchains.end()) {
            destroySwapchainData(*d, *found->second);
            d->swapchains.erase(found);
        }
    }
    d->vk.DestroySwapchainKHR(device, swapchain, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
    DeviceData *d = deviceData(queue);
    std::unique_lock lock(d->mutex);

    const filters::Values &values = settings().values();
    const auto family = d->queueFamilies.find(queue);
    if (!values.active() || d->swapchains.empty() || family == d->queueFamilies.end()
        || !(d->families[family->second].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        lock.unlock();
        return d->vk.QueuePresentKHR(queue, info);
    }

    std::vector<VkSemaphore> drawn;
    bool waitedForGame = false;
    for (uint32_t i = 0; i < info->swapchainCount; ++i) {
        const auto found = d->swapchains.find(info->pSwapchains[i]);
        if (found == d->swapchains.end() || info->pImageIndices[i] >= found->second->images.size()) {
            continue;
        }
        SwapchainData &swapchain = *found->second;
        ImageResources &image = swapchain.images[info->pImageIndices[i]];
        // The last filter draw into this image is long done by the time the
        // game presents it again; wait briefly in case it is not.
        if (d->vk.WaitForFences(d->device, 1, &image.fence, VK_TRUE, 1'000'000'000) != VK_SUCCESS
            || !commandBuffer(*d, image, family->second)) {
            continue;
        }
        d->vk.ResetFences(d->device, 1, &image.fence);
        recordFilter(*d, swapchain, image, filters::shaderParams(values, swapchain.linear));

        // The first submit takes over the game's wait; later ones follow it
        // in queue order and through the all-commands barrier.
        std::vector<VkPipelineStageFlags> stages(waitedForGame ? 0 : info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto submit = vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = waitedForGame ? 0 : info->waitSemaphoreCount;
        submit.pWaitSemaphores = info->pWaitSemaphores;
        submit.pWaitDstStageMask = stages.data();
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &image.cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &image.done;
        const VkResult result = d->vk.QueueSubmit(queue, 1, &submit, image.fence);
        if (result != VK_SUCCESS) {
            // Nothing was queued: the fence stays unsignalled, so make a new one.
            d->vk.DestroyFence(d->device, image.fence, nullptr);
            auto fence = vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            d->vk.CreateFence(d->device, &fence, nullptr, &image.fence);
            if (waitedForGame) {
                continue;
            }
            return result;
        }
        waitedForGame = true;
        drawn.push_back(image.done);
    }
    lock.unlock();

    if (!waitedForGame) {
        return d->vk.QueuePresentKHR(queue, info);
    }
    VkPresentInfoKHR filtered = *info;
    filtered.waitSemaphoreCount = uint32_t(drawn.size());
    filtered.pWaitSemaphores = drawn.data();
    return d->vk.QueuePresentKHR(queue, &filtered);
}

// ---- Lookup ----

PFN_vkVoidFunction deviceHook(const char *name)
{
#define GFXTUNE_HOOK(fn)                                                                                               \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                           \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                             \
    }
    GFXTUNE_HOOK(DestroyDevice)
    GFXTUNE_HOOK(GetDeviceQueue)
    GFXTUNE_HOOK(GetDeviceQueue2)
    GFXTUNE_HOOK(CreateSwapchainKHR)
    GFXTUNE_HOOK(DestroySwapchainKHR)
    GFXTUNE_HOOK(QueuePresentKHR)
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
    InstanceData *data = instanceData(instance);
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
    DeviceData *data = device ? deviceData(device) : nullptr;
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
