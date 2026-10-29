// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "VulkanFunctions.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace gfxtune::layer
{

// One draw of a triangle covering a whole target image: a render pass with a
// single colour attachment, a pipeline, and sampled source images bound in
// order (binding 0, 1, ...). Every filter step is one of these.
class FullscreenPass
{
public:
    struct Setup {
        VkFormat format = VK_FORMAT_UNDEFINED; // of the target
        // CLEAR when the shader may leave pixels alone (SMAA discards them).
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // UNDEFINED: old contents may go
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const std::uint32_t *vertex = nullptr;
        std::size_t vertexSize = 0; // bytes
        const std::uint32_t *fragment = nullptr;
        std::size_t fragmentSize = 0;
        std::vector<VkFilter> sources; // one sampled image per entry, with its filtering
        std::uint32_t pushSize = 0;    // fragment and vertex push constants, bytes
    };

    static std::unique_ptr<FullscreenPass> create(const DeviceFunctions &vk, VkDevice device, const Setup &setup, std::string *error);
    ~FullscreenPass();
    FullscreenPass(const FullscreenPass &) = delete;
    FullscreenPass &operator=(const FullscreenPass &) = delete;

    VkFormat format() const { return m_format; }

    VkFramebuffer createFramebuffer(VkImageView target, VkExtent2D extent, std::string *error) const;
    void destroyFramebuffer(VkFramebuffer framebuffer) const;
    // The sources, in binding order, each in SHADER_READ_ONLY_OPTIMAL when drawn.
    VkDescriptorSet createDescriptor(std::initializer_list<VkImageView> sources, std::string *error) const;
    void destroyDescriptor(VkDescriptorSet set) const;

    void record(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, VkDescriptorSet sources, const void *push) const;

private:
    FullscreenPass(const DeviceFunctions &vk, VkDevice device, VkFormat format);
    bool build(const Setup &setup, std::string *error);

    const DeviceFunctions &m_vk;
    VkDevice m_device;
    VkFormat m_format;
    std::uint32_t m_pushSize = 0;
    VkAttachmentLoadOp m_loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    std::vector<VkSampler> m_samplers;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

// A whole image of one format: the image, its memory and a view.
struct OwnImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    // `mutableFormat`: other views of a compatible format may be made.
    bool create(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memory, VkFormat format,
                VkExtent2D extent, VkImageUsageFlags usage, bool mutableFormat, std::string *error);
    void destroy(const DeviceFunctions &vk, VkDevice device);
};

VkImageView createView(const DeviceFunctions &vk, VkDevice device, VkImage image, VkFormat format);

// A memory type the device allows for `allowed`, preferring `wanted`.
bool findMemoryType(const VkPhysicalDeviceMemoryProperties &memory, std::uint32_t allowed, VkMemoryPropertyFlags wanted,
                    std::uint32_t &out);

} // namespace gfxtune::layer
