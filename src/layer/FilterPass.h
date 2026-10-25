// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "VulkanFunctions.h"
#include "filters/Filters.h"

#include <memory>
#include <string>

namespace gfxtune::layer
{

// Whether the filters can draw into images of this format and colour space,
// and if not, why (in words for the game's page).
bool supportedFormat(VkFormat format, VkColorSpaceKHR colourSpace, std::string *reason);
bool isSrgbFormat(VkFormat format);
std::string formatName(VkFormat format);

// One full-screen draw that reads a source image and writes the filtered
// picture into a target image of the same size and format. The layer uses it
// on swapchain images; the tests use it on their own images.
class FilterPass
{
public:
    // A source and target pair, ready to record.
    struct Target {
        VkImageView sourceView = VK_NULL_HANDLE;
        VkImageView targetView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkExtent2D extent{};
    };

    // `finalLayout` is the target's layout after the draw.
    static std::unique_ptr<FilterPass> create(const DeviceFunctions &vk, VkDevice device, VkFormat format,
                                              VkImageLayout finalLayout, std::string *error);
    ~FilterPass();
    FilterPass(const FilterPass &) = delete;
    FilterPass &operator=(const FilterPass &) = delete;

    VkFormat format() const { return m_format; }

    bool addTarget(VkImage source, VkImage target, VkExtent2D extent, Target &out, std::string *error);
    void removeTarget(Target &target);

    // Records the draw. The source must be in SHADER_READ_ONLY_OPTIMAL and
    // the target in COLOR_ATTACHMENT_OPTIMAL; the target ends in finalLayout.
    void record(VkCommandBuffer cmd, const Target &target, const filters::ShaderParams &params) const;

private:
    FilterPass(const DeviceFunctions &vk, VkDevice device, VkFormat format);
    bool build(VkImageLayout finalLayout, std::string *error);

    const DeviceFunctions &m_vk;
    VkDevice m_device;
    VkFormat m_format;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace gfxtune::layer
