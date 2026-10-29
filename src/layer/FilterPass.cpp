// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FilterPass.h"

// SPIR-V compiled from shaders/ at build time.
#include "filters_frag.h"
#include "fullscreen_vert.h"


namespace gfxtune::layer
{

bool isSrgbFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

std::string formatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        return "A8B8G8R8_UNORM";
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return "A8B8G8R8_SRGB";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "A2B10G10R10_UNORM";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return "A2R10G10B10_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    default:
        return "format " + std::to_string(int(format));
    }
}

bool supportedFormat(VkFormat format, VkColorSpaceKHR colourSpace, std::string *reason)
{
    if (colourSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        if (reason) {
            *reason = "the game shows HDR or a wide colour space";
        }
        return false;
    }
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return true;
    default:
        if (reason) {
            *reason = "the game's picture format (" + formatName(format) + ") is not supported yet";
        }
        return false;
    }
}

FilterPass::FilterPass(const DeviceFunctions &vk, VkDevice device, std::unique_ptr<FullscreenPass> pass)
    : m_vk(vk)
    , m_device(device)
    , m_pass(std::move(pass))
{
}

std::unique_ptr<FilterPass> FilterPass::create(const DeviceFunctions &vk, VkDevice device, VkFormat format,
                                               VkImageLayout finalLayout, std::string *error)
{
    FullscreenPass::Setup setup;
    setup.format = format;
    setup.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel is drawn
    setup.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    setup.finalLayout = finalLayout;
    setup.vertex = fullscreen_vert;
    setup.vertexSize = sizeof(fullscreen_vert);
    setup.fragment = filters_frag;
    setup.fragmentSize = sizeof(filters_frag);
    setup.sources = {VK_FILTER_NEAREST};
    setup.pushSize = sizeof(filters::ShaderParams);
    std::unique_ptr<FullscreenPass> pass = FullscreenPass::create(vk, device, setup, error);
    if (!pass) {
        return nullptr;
    }
    return std::unique_ptr<FilterPass>(new FilterPass(vk, device, std::move(pass)));
}

bool FilterPass::addTarget(VkImage source, VkImage target, VkExtent2D extent, Target &out, std::string *error)
{
    out = Target{};
    out.extent = extent;
    out.sourceView = createView(m_vk, m_device, source, m_pass->format());
    out.targetView = createView(m_vk, m_device, target, m_pass->format());
    if (out.sourceView == VK_NULL_HANDLE || out.targetView == VK_NULL_HANDLE) {
        if (error) {
            *error = "vkCreateImageView failed";
        }
        removeTarget(out);
        return false;
    }
    out.framebuffer = m_pass->createFramebuffer(out.targetView, extent, error);
    out.descriptor = out.framebuffer != VK_NULL_HANDLE ? m_pass->createDescriptor({out.sourceView}, error) : VK_NULL_HANDLE;
    if (out.descriptor == VK_NULL_HANDLE) {
        removeTarget(out);
        return false;
    }
    return true;
}

void FilterPass::removeTarget(Target &target)
{
    m_pass->destroyDescriptor(target.descriptor);
    m_pass->destroyFramebuffer(target.framebuffer);
    m_vk.DestroyImageView(m_device, target.targetView, nullptr);
    m_vk.DestroyImageView(m_device, target.sourceView, nullptr);
    target = Target{};
}

void FilterPass::record(VkCommandBuffer cmd, const Target &target, const filters::ShaderParams &params) const
{
    m_pass->record(cmd, target.framebuffer, target.extent, target.descriptor, &params);
}

} // namespace gfxtune::layer
