// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "FullscreenPass.h"
#include "VulkanFunctions.h"

#include <memory>
#include <string>

namespace gfxtune::layer
{

// SMAA 1x anti-aliasing (Jimenez et al., MIT; see third_party/smaa), with its
// "high" preset: find the edges in the game's picture, work out how to blend
// across them, and blend into an anti-aliased picture of the same format,
// which the sharpening and colour step then reads.
class Smaa
{
public:
    // Per swapchain: the work images, shared by all of its images. Frames
    // follow each other on one queue, and every pass waits for the one
    // before, so one set is enough.
    struct Frame {
        VkExtent2D extent{};
        OwnImage edges;   // R8G8
        OwnImage weights; // R8G8B8A8
        OwnImage output;  // the game's format
        VkFramebuffer edgesTarget = VK_NULL_HANDLE;
        VkFramebuffer weightsTarget = VK_NULL_HANDLE;
        VkFramebuffer outputTarget = VK_NULL_HANDLE;
        VkDescriptorSet weightsSources = VK_NULL_HANDLE;
    };

    // Per source image (the layer's copy of each swapchain image).
    struct Source {
        VkImageView encoded = VK_NULL_HANDLE; // display-encoded values, for finding edges
        VkImageView native = VK_NULL_HANDLE;  // its own format (linear light if sRGB), for blending
        VkDescriptorSet edgesSources = VK_NULL_HANDLE;
        VkDescriptorSet blendSources = VK_NULL_HANDLE;
    };

    static std::unique_ptr<Smaa> create(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memory,
                                        VkFormat format, std::string *error);
    ~Smaa();
    Smaa(const Smaa &) = delete;
    Smaa &operator=(const Smaa &) = delete;

    bool createFrame(VkExtent2D extent, Frame &out, std::string *error);
    void destroyFrame(Frame &frame);
    // The source image must allow a view of its plain (non-sRGB) format:
    // made with VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT when it is sRGB.
    bool createSource(const Frame &frame, VkImage image, Source &out, std::string *error);
    void destroySource(Source &source);

    // The three passes. The source is in SHADER_READ_ONLY_OPTIMAL; so is
    // frame.output afterwards. The first recording also uploads SMAA's
    // lookup textures: call uploadSubmitted() once it went to the GPU.
    void record(VkCommandBuffer cmd, const Frame &frame, const Source &source);
    void uploadSubmitted() { m_uploaded = true; }

    // The plain format an sRGB format is stored as.
    static VkFormat encodedFormat(VkFormat format);

private:
    Smaa(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memory, VkFormat format);
    bool build(std::string *error);
    void recordUpload(VkCommandBuffer cmd);

    const DeviceFunctions &m_vk;
    VkDevice m_device;
    VkPhysicalDeviceMemoryProperties m_memory;
    VkFormat m_format;

    std::unique_ptr<FullscreenPass> m_edges;
    std::unique_ptr<FullscreenPass> m_weights;
    std::unique_ptr<FullscreenPass> m_blend;

    OwnImage m_area;   // R8G8, 160x560
    OwnImage m_search; // R8, 64x16
    VkBuffer m_staging = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    bool m_uploaded = false;
};

} // namespace gfxtune::layer
