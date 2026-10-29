// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Smaa.h"

// SPIR-V compiled from shaders/ at build time.
#include "smaa_blend_frag.h"
#include "smaa_blend_vert.h"
#include "smaa_edges_frag.h"
#include "smaa_edges_vert.h"
#include "smaa_weights_frag.h"
#include "smaa_weights_vert.h"

// SMAA's precomputed lookup textures.
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"

#include <cstring>

namespace gfxtune::layer
{

namespace
{

struct Metrics {
    float inverseWidth, inverseHeight, width, height;
};

Metrics metricsFor(VkExtent2D extent)
{
    return {1.0f / float(extent.width), 1.0f / float(extent.height), float(extent.width), float(extent.height)};
}

} // namespace

VkFormat Smaa::encodedFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    default:
        return format;
    }
}

Smaa::Smaa(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memory, VkFormat format)
    : m_vk(vk)
    , m_device(device)
    , m_memory(memory)
    , m_format(format)
{
}

std::unique_ptr<Smaa> Smaa::create(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memory,
                                   VkFormat format, std::string *error)
{
    std::unique_ptr<Smaa> smaa(new Smaa(vk, device, memory, format));
    if (!smaa->build(error)) {
        return nullptr;
    }
    return smaa;
}

bool Smaa::build(std::string *error)
{
    FullscreenPass::Setup edges;
    edges.format = VK_FORMAT_R8G8_UNORM;
    edges.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // pixels without an edge are discarded
    edges.vertex = smaa_edges_vert;
    edges.vertexSize = sizeof(smaa_edges_vert);
    edges.fragment = smaa_edges_frag;
    edges.fragmentSize = sizeof(smaa_edges_frag);
    edges.sources = {VK_FILTER_NEAREST};
    edges.pushSize = sizeof(Metrics);
    m_edges = FullscreenPass::create(m_vk, m_device, edges, error);

    FullscreenPass::Setup weights = edges;
    weights.format = VK_FORMAT_R8G8B8A8_UNORM;
    weights.vertex = smaa_weights_vert;
    weights.vertexSize = sizeof(smaa_weights_vert);
    weights.fragment = smaa_weights_frag;
    weights.fragmentSize = sizeof(smaa_weights_frag);
    // Edges and areas are read between texels on purpose; the search
    // texture is a table and must not be.
    weights.sources = {VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_FILTER_NEAREST};
    m_weights = m_edges ? FullscreenPass::create(m_vk, m_device, weights, error) : nullptr;

    FullscreenPass::Setup blend = edges;
    blend.format = m_format;
    blend.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel is drawn
    blend.vertex = smaa_blend_vert;
    blend.vertexSize = sizeof(smaa_blend_vert);
    blend.fragment = smaa_blend_frag;
    blend.fragmentSize = sizeof(smaa_blend_frag);
    blend.sources = {VK_FILTER_LINEAR, VK_FILTER_LINEAR};
    m_blend = m_weights ? FullscreenPass::create(m_vk, m_device, blend, error) : nullptr;
    if (!m_blend) {
        return false;
    }

    const VkImageUsageFlags lookup = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!m_area.create(m_vk, m_device, m_memory, VK_FORMAT_R8G8_UNORM, {AREATEX_WIDTH, AREATEX_HEIGHT}, lookup, false, error)
        || !m_search.create(m_vk, m_device, m_memory, VK_FORMAT_R8_UNORM, {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT}, lookup, false, error)) {
        return false;
    }

    // Both tables in one host-visible buffer, area first.
    auto buffer = vkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = sizeof(areaTexBytes) + sizeof(searchTexBytes);
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (m_vk.CreateBuffer(m_device, &buffer, nullptr, &m_staging) != VK_SUCCESS) {
        m_staging = VK_NULL_HANDLE;
        *error = "could not create SMAA's upload buffer";
        return false;
    }
    VkMemoryRequirements needs{};
    m_vk.GetBufferMemoryRequirements(m_device, m_staging, &needs);
    auto allocate = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocate.allocationSize = needs.size;
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t type = 0;
    bool hostVisible = false;
    for (std::uint32_t i = 0; i < m_memory.memoryTypeCount; ++i) {
        if ((needs.memoryTypeBits & (1u << i)) && (m_memory.memoryTypes[i].propertyFlags & host) == host) {
            type = i;
            hostVisible = true;
            break;
        }
    }
    allocate.memoryTypeIndex = type;
    void *mapped = nullptr;
    if (!hostVisible || m_vk.AllocateMemory(m_device, &allocate, nullptr, &m_stagingMemory) != VK_SUCCESS
        || m_vk.BindBufferMemory(m_device, m_staging, m_stagingMemory, 0) != VK_SUCCESS
        || m_vk.MapMemory(m_device, m_stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        *error = "could not fill SMAA's upload buffer";
        return false;
    }
    std::memcpy(mapped, areaTexBytes, sizeof(areaTexBytes));
    std::memcpy(static_cast<char *>(mapped) + sizeof(areaTexBytes), searchTexBytes, sizeof(searchTexBytes));
    m_vk.UnmapMemory(m_device, m_stagingMemory);
    return true;
}

Smaa::~Smaa()
{
    m_vk.DestroyBuffer(m_device, m_staging, nullptr);
    m_vk.FreeMemory(m_device, m_stagingMemory, nullptr);
    m_area.destroy(m_vk, m_device);
    m_search.destroy(m_vk, m_device);
}

bool Smaa::createFrame(VkExtent2D extent, Frame &out, std::string *error)
{
    out = Frame{};
    out.extent = extent;
    const VkImageUsageFlags work = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const bool ok = out.edges.create(m_vk, m_device, m_memory, VK_FORMAT_R8G8_UNORM, extent, work, false, error)
        && out.weights.create(m_vk, m_device, m_memory, VK_FORMAT_R8G8B8A8_UNORM, extent, work, false, error)
        && out.output.create(m_vk, m_device, m_memory, m_format, extent, work, false, error)
        && (out.edgesTarget = m_edges->createFramebuffer(out.edges.view, extent, error)) != VK_NULL_HANDLE
        && (out.weightsTarget = m_weights->createFramebuffer(out.weights.view, extent, error)) != VK_NULL_HANDLE
        && (out.outputTarget = m_blend->createFramebuffer(out.output.view, extent, error)) != VK_NULL_HANDLE
        && (out.weightsSources = m_weights->createDescriptor({out.edges.view, m_area.view, m_search.view}, error)) != VK_NULL_HANDLE;
    if (!ok) {
        destroyFrame(out);
    }
    return ok;
}

void Smaa::destroyFrame(Frame &frame)
{
    m_weights->destroyDescriptor(frame.weightsSources);
    m_edges->destroyFramebuffer(frame.edgesTarget);
    m_weights->destroyFramebuffer(frame.weightsTarget);
    m_blend->destroyFramebuffer(frame.outputTarget);
    frame.edges.destroy(m_vk, m_device);
    frame.weights.destroy(m_vk, m_device);
    frame.output.destroy(m_vk, m_device);
    frame = Frame{};
}

bool Smaa::createSource(const Frame &frame, VkImage image, Source &out, std::string *error)
{
    out = Source{};
    out.encoded = createView(m_vk, m_device, image, encodedFormat(m_format));
    out.native = createView(m_vk, m_device, image, m_format);
    const bool ok = out.encoded != VK_NULL_HANDLE && out.native != VK_NULL_HANDLE
        && (out.edgesSources = m_edges->createDescriptor({out.encoded}, error)) != VK_NULL_HANDLE
        && (out.blendSources = m_blend->createDescriptor({out.native, frame.weights.view}, error)) != VK_NULL_HANDLE;
    if (!ok) {
        if (error && error->empty()) {
            *error = "vkCreateImageView failed";
        }
        destroySource(out);
    }
    return ok;
}

void Smaa::destroySource(Source &source)
{
    m_edges->destroyDescriptor(source.edgesSources);
    m_blend->destroyDescriptor(source.blendSources);
    m_vk.DestroyImageView(m_device, source.encoded, nullptr);
    m_vk.DestroyImageView(m_device, source.native, nullptr);
    source = Source{};
}

void Smaa::recordUpload(VkCommandBuffer cmd)
{
    const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier barriers[2]{};
    for (VkImageMemoryBarrier &b : barriers) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = whole;
    }
    barriers[0].image = m_area.image;
    barriers[1].image = m_search.image;
    m_vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

    VkBufferImageCopy area{};
    area.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    area.imageExtent = {AREATEX_WIDTH, AREATEX_HEIGHT, 1};
    VkBufferImageCopy search = area;
    search.bufferOffset = sizeof(areaTexBytes);
    search.imageExtent = {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1};
    m_vk.CmdCopyBufferToImage(cmd, m_staging, m_area.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &area);
    m_vk.CmdCopyBufferToImage(cmd, m_staging, m_search.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &search);

    for (VkImageMemoryBarrier &b : barriers) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    m_vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
}

void Smaa::record(VkCommandBuffer cmd, const Frame &frame, const Source &source)
{
    if (!m_uploaded) {
        recordUpload(cmd);
    }
    const Metrics metrics = metricsFor(frame.extent);
    m_edges->record(cmd, frame.edgesTarget, frame.extent, source.edgesSources, &metrics);
    m_weights->record(cmd, frame.weightsTarget, frame.extent, frame.weightsSources, &metrics);
    m_blend->record(cmd, frame.outputTarget, frame.extent, source.blendSources, &metrics);
}

} // namespace gfxtune::layer
