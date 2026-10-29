// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <vulkan/vulkan.h>

#include <string_view>

namespace gfxtune::layer
{

// A Vulkan structure with every field zero and its type set.
template<typename T>
T vkStruct(VkStructureType type)
{
    T value{};
    value.sType = type;
    return value;
}

// The Vulkan functions the layer calls, looked up from the next layer down
// (or, in tests, straight from the loader).

#define GFXTUNE_INSTANCE_FUNCTIONS(X)                                                                                  \
    X(DestroyInstance)                                                                                                 \
    X(GetPhysicalDeviceFormatProperties)                                                                               \
    X(GetPhysicalDeviceProperties)                                                                                     \
    X(GetPhysicalDeviceMemoryProperties)                                                                               \
    X(GetPhysicalDeviceQueueFamilyProperties)                                                                          \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR)

#define GFXTUNE_DEVICE_FUNCTIONS(X)                                                                                    \
    X(DestroyDevice)                                                                                                   \
    X(GetDeviceQueue)                                                                                                  \
    X(GetDeviceQueue2)                                                                                                 \
    X(CreateSwapchainKHR)                                                                                              \
    X(DestroySwapchainKHR)                                                                                             \
    X(GetSwapchainImagesKHR)                                                                                           \
    X(QueuePresentKHR)                                                                                                 \
    X(QueueSubmit)                                                                                                     \
    X(DeviceWaitIdle)                                                                                                  \
    X(CreateBuffer)                                                                                                    \
    X(DestroyBuffer)                                                                                                   \
    X(GetBufferMemoryRequirements)                                                                                     \
    X(BindBufferMemory)                                                                                                \
    X(MapMemory)                                                                                                       \
    X(UnmapMemory)                                                                                                     \
    X(CmdCopyBufferToImage)                                                                                            \
    X(CreateImage)                                                                                                     \
    X(DestroyImage)                                                                                                    \
    X(GetImageMemoryRequirements)                                                                                      \
    X(AllocateMemory)                                                                                                  \
    X(FreeMemory)                                                                                                      \
    X(BindImageMemory)                                                                                                 \
    X(CreateImageView)                                                                                                 \
    X(DestroyImageView)                                                                                                \
    X(CreateSampler)                                                                                                   \
    X(DestroySampler)                                                                                                  \
    X(CreateDescriptorSetLayout)                                                                                       \
    X(DestroyDescriptorSetLayout)                                                                                      \
    X(CreateDescriptorPool)                                                                                            \
    X(DestroyDescriptorPool)                                                                                           \
    X(AllocateDescriptorSets)                                                                                          \
    X(FreeDescriptorSets)                                                                                              \
    X(UpdateDescriptorSets)                                                                                            \
    X(CreatePipelineLayout)                                                                                            \
    X(DestroyPipelineLayout)                                                                                           \
    X(CreateShaderModule)                                                                                              \
    X(DestroyShaderModule)                                                                                             \
    X(CreateGraphicsPipelines)                                                                                         \
    X(DestroyPipeline)                                                                                                 \
    X(CreateRenderPass)                                                                                                \
    X(DestroyRenderPass)                                                                                               \
    X(CreateFramebuffer)                                                                                               \
    X(DestroyFramebuffer)                                                                                              \
    X(CreateCommandPool)                                                                                               \
    X(DestroyCommandPool)                                                                                              \
    X(AllocateCommandBuffers)                                                                                          \
    X(FreeCommandBuffers)                                                                                              \
    X(BeginCommandBuffer)                                                                                              \
    X(EndCommandBuffer)                                                                                                \
    X(CmdPipelineBarrier)                                                                                              \
    X(CmdCopyImage)                                                                                                    \
    X(CmdBeginRenderPass)                                                                                              \
    X(CmdEndRenderPass)                                                                                                \
    X(CmdBindPipeline)                                                                                                 \
    X(CmdBindDescriptorSets)                                                                                           \
    X(CmdPushConstants)                                                                                                \
    X(CmdSetViewport)                                                                                                  \
    X(CmdSetScissor)                                                                                                   \
    X(CmdDraw)                                                                                                         \
    X(CreateSemaphore)                                                                                                 \
    X(DestroySemaphore)                                                                                                \
    X(CreateFence)                                                                                                     \
    X(DestroyFence)                                                                                                    \
    X(WaitForFences)                                                                                                   \
    X(ResetFences)

#define GFXTUNE_DECLARE(name) PFN_vk##name name = nullptr;

struct InstanceFunctions {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    GFXTUNE_INSTANCE_FUNCTIONS(GFXTUNE_DECLARE)

    void load(VkInstance instance, PFN_vkGetInstanceProcAddr getProcAddr)
    {
        GetInstanceProcAddr = getProcAddr;
#define GFXTUNE_LOAD(name) name = reinterpret_cast<PFN_vk##name>(getProcAddr(instance, "vk" #name));
        GFXTUNE_INSTANCE_FUNCTIONS(GFXTUNE_LOAD)
#undef GFXTUNE_LOAD
    }
};

struct DeviceFunctions {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    GFXTUNE_DEVICE_FUNCTIONS(GFXTUNE_DECLARE)

    void load(VkDevice device, PFN_vkGetDeviceProcAddr getProcAddr)
    {
        GetDeviceProcAddr = getProcAddr;
#define GFXTUNE_LOAD(name) name = reinterpret_cast<PFN_vk##name>(getProcAddr(device, "vk" #name));
        GFXTUNE_DEVICE_FUNCTIONS(GFXTUNE_LOAD)
#undef GFXTUNE_LOAD
    }

    // Everything the filters need, so a device without one of them is left
    // alone instead of crashing. GetDeviceQueue2 is optional (Vulkan 1.1).
    bool complete() const
    {
        bool ok = true;
#define GFXTUNE_CHECK(name) ok = ok && (name != nullptr || std::string_view(#name) == "GetDeviceQueue2");
        GFXTUNE_DEVICE_FUNCTIONS(GFXTUNE_CHECK)
#undef GFXTUNE_CHECK
        return ok;
    }
};

#undef GFXTUNE_DECLARE

} // namespace gfxtune::layer
