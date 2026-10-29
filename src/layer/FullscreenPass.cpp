// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FullscreenPass.h"

#include <iterator>

namespace gfxtune::layer
{

namespace
{

constexpr std::uint32_t MaxSets = 32;

bool failed(VkResult result, const char *what, std::string *error)
{
    if (result == VK_SUCCESS) {
        return false;
    }
    if (error) {
        *error = std::string(what) + " failed (VkResult " + std::to_string(int(result)) + ")";
    }
    return true;
}

} // namespace

bool findMemoryType(const VkPhysicalDeviceMemoryProperties &memory, std::uint32_t allowed, VkMemoryPropertyFlags wanted, std::uint32_t &out)
{
    for (const VkMemoryPropertyFlags flags : {wanted, VkMemoryPropertyFlags(0)}) {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((allowed & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
                out = i;
                return true;
            }
        }
    }
    return false;
}

VkImageView createView(const DeviceFunctions &vk, VkDevice device, VkImage image, VkFormat format)
{
    auto info = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    return vk.CreateImageView(device, &info, nullptr, &view) == VK_SUCCESS ? view : VK_NULL_HANDLE;
}

bool OwnImage::create(const DeviceFunctions &vk, VkDevice device, const VkPhysicalDeviceMemoryProperties &memoryTypes, VkFormat format,
                      VkExtent2D extent, VkImageUsageFlags usage, bool mutableFormat, std::string *error)
{
    auto info = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.flags = mutableFormat ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (failed(vk.CreateImage(device, &info, nullptr, &image), "vkCreateImage", error)) {
        image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements needs{};
    vk.GetImageMemoryRequirements(device, image, &needs);
    auto allocate = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocate.allocationSize = needs.size;
    if (!findMemoryType(memoryTypes, needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, allocate.memoryTypeIndex)
        || vk.AllocateMemory(device, &allocate, nullptr, &memory) != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        if (error) {
            *error = "not enough video memory for the filters";
        }
        return false;
    }
    if (failed(vk.BindImageMemory(device, image, memory, 0), "vkBindImageMemory", error)) {
        return false;
    }
    view = createView(vk, device, image, format);
    if (view == VK_NULL_HANDLE) {
        if (error) {
            *error = "vkCreateImageView failed";
        }
        return false;
    }
    return true;
}

void OwnImage::destroy(const DeviceFunctions &vk, VkDevice device)
{
    vk.DestroyImageView(device, view, nullptr);
    vk.DestroyImage(device, image, nullptr);
    vk.FreeMemory(device, memory, nullptr);
    *this = OwnImage{};
}

FullscreenPass::FullscreenPass(const DeviceFunctions &vk, VkDevice device, VkFormat format)
    : m_vk(vk)
    , m_device(device)
    , m_format(format)
{
}

std::unique_ptr<FullscreenPass> FullscreenPass::create(const DeviceFunctions &vk, VkDevice device, const Setup &setup, std::string *error)
{
    std::unique_ptr<FullscreenPass> pass(new FullscreenPass(vk, device, setup.format));
    if (!pass->build(setup, error)) {
        return nullptr;
    }
    return pass;
}

bool FullscreenPass::build(const Setup &setup, std::string *error)
{
    m_pushSize = setup.pushSize;
    m_loadOp = setup.loadOp;

    // Clamped at the edges; texelFetch() ignores the filtering anyway.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    m_samplers.resize(setup.sources.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < setup.sources.size(); ++i) {
        auto sampler = vkStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        sampler.magFilter = setup.sources[i];
        sampler.minFilter = setup.sources[i];
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.maxLod = VK_LOD_CLAMP_NONE; // the layer's own sources have one level anyway
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (failed(m_vk.CreateSampler(m_device, &sampler, nullptr, &m_samplers[i]), "vkCreateSampler", error)) {
            return false;
        }
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = std::uint32_t(i);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = &m_samplers[i];
        bindings.push_back(binding);
    }
    auto setLayout = vkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    setLayout.bindingCount = std::uint32_t(bindings.size());
    setLayout.pBindings = bindings.data();
    if (failed(m_vk.CreateDescriptorSetLayout(m_device, &setLayout, nullptr, &m_setLayout), "vkCreateDescriptorSetLayout", error)) {
        return false;
    }
    if (!bindings.empty()) {
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxSets * std::uint32_t(bindings.size())};
        auto pool = vkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = MaxSets;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        if (failed(m_vk.CreateDescriptorPool(m_device, &pool, nullptr, &m_pool), "vkCreateDescriptorPool", error)) {
            return false;
        }
    }

    const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, m_pushSize};
    auto layout = vkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &m_setLayout;
    layout.pushConstantRangeCount = m_pushSize > 0 ? 1 : 0;
    layout.pPushConstantRanges = &pushRange;
    if (failed(m_vk.CreatePipelineLayout(m_device, &layout, nullptr, &m_pipelineLayout), "vkCreatePipelineLayout", error)) {
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = m_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = setup.loadOp;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = setup.initialLayout;
    attachment.finalLayout = setup.finalLayout;
    VkAttachmentReference colourRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colourRef;
    // Before: everything earlier on the queue, so a target reused from the
    // last frame is done being read and written. After: anything later may
    // read the result (the next pass, presentation, a copy in the tests).
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    auto renderPass = vkStruct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    renderPass.attachmentCount = 1;
    renderPass.pAttachments = &attachment;
    renderPass.subpassCount = 1;
    renderPass.pSubpasses = &subpass;
    renderPass.dependencyCount = 2;
    renderPass.pDependencies = dependencies;
    if (failed(m_vk.CreateRenderPass(m_device, &renderPass, nullptr, &m_renderPass), "vkCreateRenderPass", error)) {
        return false;
    }

    auto vertexCode = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    vertexCode.codeSize = setup.vertexSize;
    vertexCode.pCode = setup.vertex;
    auto fragmentCode = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    fragmentCode.codeSize = setup.fragmentSize;
    fragmentCode.pCode = setup.fragment;
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    if (failed(m_vk.CreateShaderModule(m_device, &vertexCode, nullptr, &vertex), "vkCreateShaderModule", error)) {
        return false;
    }
    if (failed(m_vk.CreateShaderModule(m_device, &fragmentCode, nullptr, &fragment), "vkCreateShaderModule", error)) {
        m_vk.DestroyShaderModule(m_device, vertex, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";
    auto vertexInput = vkStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = vkStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = vkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = vkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    auto multisample = vkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = vkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = vkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = std::uint32_t(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;

    auto pipeline = vkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = m_pipelineLayout;
    pipeline.renderPass = m_renderPass;
    const VkResult result = m_vk.CreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline);
    m_vk.DestroyShaderModule(m_device, vertex, nullptr);
    m_vk.DestroyShaderModule(m_device, fragment, nullptr);
    return !failed(result, "vkCreateGraphicsPipelines", error);
}

FullscreenPass::~FullscreenPass()
{
    // Destroying a null handle is allowed, so a half-built pass is fine.
    m_vk.DestroyPipeline(m_device, m_pipeline, nullptr);
    m_vk.DestroyRenderPass(m_device, m_renderPass, nullptr);
    m_vk.DestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_vk.DestroyDescriptorPool(m_device, m_pool, nullptr);
    m_vk.DestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    for (VkSampler sampler : m_samplers) {
        m_vk.DestroySampler(m_device, sampler, nullptr);
    }
}

VkFramebuffer FullscreenPass::createFramebuffer(VkImageView target, VkExtent2D extent, std::string *error) const
{
    auto info = vkStruct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    info.renderPass = m_renderPass;
    info.attachmentCount = 1;
    info.pAttachments = &target;
    info.width = extent.width;
    info.height = extent.height;
    info.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (failed(m_vk.CreateFramebuffer(m_device, &info, nullptr, &framebuffer), "vkCreateFramebuffer", error)) {
        return VK_NULL_HANDLE;
    }
    return framebuffer;
}

void FullscreenPass::destroyFramebuffer(VkFramebuffer framebuffer) const
{
    m_vk.DestroyFramebuffer(m_device, framebuffer, nullptr);
}

VkDescriptorSet FullscreenPass::createDescriptor(std::initializer_list<VkImageView> sources, std::string *error) const
{
    if (m_pool == VK_NULL_HANDLE || sources.size() != m_samplers.size()) {
        if (error) {
            *error = "the filter step was given the wrong number of images";
        }
        return VK_NULL_HANDLE;
    }
    auto allocate = vkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = m_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &m_setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (failed(m_vk.AllocateDescriptorSets(m_device, &allocate, &set), "vkAllocateDescriptorSets", error)) {
        return VK_NULL_HANDLE;
    }
    std::vector<VkDescriptorImageInfo> images;
    for (VkImageView view : sources) {
        images.push_back({VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
    std::vector<VkWriteDescriptorSet> writes;
    for (std::size_t i = 0; i < images.size(); ++i) {
        auto write = vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = set;
        write.dstBinding = std::uint32_t(i);
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &images[i];
        writes.push_back(write);
    }
    m_vk.UpdateDescriptorSets(m_device, std::uint32_t(writes.size()), writes.data(), 0, nullptr);
    return set;
}

void FullscreenPass::destroyDescriptor(VkDescriptorSet set) const
{
    if (set != VK_NULL_HANDLE) {
        m_vk.FreeDescriptorSets(m_device, m_pool, 1, &set);
    }
}

void FullscreenPass::record(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, VkDescriptorSet sources, const void *push) const
{
    VkClearValue clear{};
    auto begin = vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    begin.renderPass = m_renderPass;
    begin.framebuffer = framebuffer;
    begin.renderArea = {{0, 0}, extent};
    begin.clearValueCount = m_loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? 1 : 0;
    begin.pClearValues = &clear;
    m_vk.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    m_vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    if (sources != VK_NULL_HANDLE) {
        m_vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &sources, 0, nullptr);
    }
    if (m_pushSize > 0) {
        m_vk.CmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, m_pushSize, push);
    }
    const VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    m_vk.CmdSetViewport(cmd, 0, 1, &viewport);
    m_vk.CmdSetScissor(cmd, 0, 1, &scissor);
    m_vk.CmdDraw(cmd, 3, 1, 0, 0);
    m_vk.CmdEndRenderPass(cmd);
}

} // namespace gfxtune::layer
