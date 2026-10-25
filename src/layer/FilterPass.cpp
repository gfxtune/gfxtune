// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "FilterPass.h"

// SPIR-V compiled from shaders/ at build time.
#include "filters_frag.h"
#include "fullscreen_vert.h"

#include <iterator>

namespace gfxtune::layer
{

namespace
{

constexpr uint32_t MaxTargets = 32;

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

FilterPass::FilterPass(const DeviceFunctions &vk, VkDevice device, VkFormat format)
    : m_vk(vk)
    , m_device(device)
    , m_format(format)
{
}

std::unique_ptr<FilterPass> FilterPass::create(const DeviceFunctions &vk, VkDevice device, VkFormat format,
                                               VkImageLayout finalLayout, std::string *error)
{
    std::unique_ptr<FilterPass> pass(new FilterPass(vk, device, format));
    if (!pass->build(finalLayout, error)) {
        return nullptr;
    }
    return pass;
}

bool FilterPass::build(VkImageLayout finalLayout, std::string *error)
{
    auto sampler = vkStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (failed(m_vk.CreateSampler(m_device, &sampler, nullptr, &m_sampler), "vkCreateSampler", error)) {
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &m_sampler;
    auto setLayout = vkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    setLayout.bindingCount = 1;
    setLayout.pBindings = &binding;
    if (failed(m_vk.CreateDescriptorSetLayout(m_device, &setLayout, nullptr, &m_setLayout), "vkCreateDescriptorSetLayout", error)) {
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxTargets};
    auto pool = vkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = MaxTargets;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &poolSize;
    if (failed(m_vk.CreateDescriptorPool(m_device, &pool, nullptr, &m_pool), "vkCreateDescriptorPool", error)) {
        return false;
    }

    VkPushConstantRange pushRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(filters::ShaderParams)};
    auto layout = vkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &m_setLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &pushRange;
    if (failed(m_vk.CreatePipelineLayout(m_device, &layout, nullptr, &m_pipelineLayout), "vkCreatePipelineLayout", error)) {
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = m_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel is drawn
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = finalLayout;
    VkAttachmentReference colourRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colourRef;
    // The caller's barriers order the work before; afterwards, anything
    // (presentation, a copy in the tests) may read the result.
    VkSubpassDependency after{};
    after.srcSubpass = 0;
    after.dstSubpass = VK_SUBPASS_EXTERNAL;
    after.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    after.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    after.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    auto renderPass = vkStruct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    renderPass.attachmentCount = 1;
    renderPass.pAttachments = &attachment;
    renderPass.subpassCount = 1;
    renderPass.pSubpasses = &subpass;
    renderPass.dependencyCount = 1;
    renderPass.pDependencies = &after;
    if (failed(m_vk.CreateRenderPass(m_device, &renderPass, nullptr, &m_renderPass), "vkCreateRenderPass", error)) {
        return false;
    }

    auto vertexCode = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    vertexCode.codeSize = sizeof(fullscreen_vert);
    vertexCode.pCode = fullscreen_vert;
    auto fragmentCode = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    fragmentCode.codeSize = sizeof(filters_frag);
    fragmentCode.pCode = filters_frag;
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
    dynamic.dynamicStateCount = uint32_t(std::size(dynamicStates));
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

FilterPass::~FilterPass()
{
    // Destroying a null handle is allowed, so a half-built pass is fine.
    m_vk.DestroyPipeline(m_device, m_pipeline, nullptr);
    m_vk.DestroyRenderPass(m_device, m_renderPass, nullptr);
    m_vk.DestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_vk.DestroyDescriptorPool(m_device, m_pool, nullptr);
    m_vk.DestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    m_vk.DestroySampler(m_device, m_sampler, nullptr);
}

bool FilterPass::addTarget(VkImage source, VkImage target, VkExtent2D extent, Target &out, std::string *error)
{
    out = Target{};
    out.extent = extent;

    auto view = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = m_format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    view.image = source;
    if (failed(m_vk.CreateImageView(m_device, &view, nullptr, &out.sourceView), "vkCreateImageView", error)) {
        removeTarget(out);
        return false;
    }
    view.image = target;
    if (failed(m_vk.CreateImageView(m_device, &view, nullptr, &out.targetView), "vkCreateImageView", error)) {
        removeTarget(out);
        return false;
    }

    auto framebuffer = vkStruct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    framebuffer.renderPass = m_renderPass;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &out.targetView;
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1;
    if (failed(m_vk.CreateFramebuffer(m_device, &framebuffer, nullptr, &out.framebuffer), "vkCreateFramebuffer", error)) {
        removeTarget(out);
        return false;
    }

    auto allocate = vkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = m_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &m_setLayout;
    if (failed(m_vk.AllocateDescriptorSets(m_device, &allocate, &out.descriptor), "vkAllocateDescriptorSets", error)) {
        out.descriptor = VK_NULL_HANDLE;
        removeTarget(out);
        return false;
    }
    VkDescriptorImageInfo image{VK_NULL_HANDLE, out.sourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = out.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    m_vk.UpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

void FilterPass::removeTarget(Target &target)
{
    if (target.descriptor != VK_NULL_HANDLE) {
        m_vk.FreeDescriptorSets(m_device, m_pool, 1, &target.descriptor);
    }
    m_vk.DestroyFramebuffer(m_device, target.framebuffer, nullptr);
    m_vk.DestroyImageView(m_device, target.targetView, nullptr);
    m_vk.DestroyImageView(m_device, target.sourceView, nullptr);
    target = Target{};
}

void FilterPass::record(VkCommandBuffer cmd, const Target &target, const filters::ShaderParams &params) const
{
    auto begin = vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    begin.renderPass = m_renderPass;
    begin.framebuffer = target.framebuffer;
    begin.renderArea = {{0, 0}, target.extent};
    m_vk.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    m_vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &target.descriptor, 0, nullptr);
    m_vk.CmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
    const VkViewport viewport{0.0f, 0.0f, float(target.extent.width), float(target.extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, target.extent};
    m_vk.CmdSetViewport(cmd, 0, 1, &viewport);
    m_vk.CmdSetScissor(cmd, 0, 1, &scissor);
    m_vk.CmdDraw(cmd, 3, 1, 0, 0);
    m_vk.CmdEndRenderPass(cmd);
}

} // namespace gfxtune::layer
