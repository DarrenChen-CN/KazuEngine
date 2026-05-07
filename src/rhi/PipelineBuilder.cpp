// ============================================================================
// KazuEngine - RHI Layer: PipelineBuilder (Implementation)
// ============================================================================

#include "PipelineBuilder.h"
#include "../core/Utils.h"
#include <spdlog/spdlog.h>
#include <map>

namespace kazu {

// Helper: merge descriptor bindings across shader stages
static std::vector<ShaderDescriptorBinding> mergeDescriptorBindings(
    const std::vector<ShaderReflection>& reflections) {
    std::map<std::pair<uint32_t, uint32_t>, ShaderDescriptorBinding> merged;
    for (const auto& refl : reflections) {
        for (const auto& b : refl.descriptorBindings) {
            auto key = std::make_pair(b.set, b.binding);
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged[key] = b;
            } else {
                it->second.stageFlags |= b.stageFlags;
            }
        }
    }
    std::vector<ShaderDescriptorBinding> result;
    result.reserve(merged.size());
    for (auto& [key, binding] : merged) {
        result.push_back(binding);
    }
    return result;
}

// Helper: merge push constant ranges across shader stages
static std::vector<ShaderPushConstantRange> mergePushConstantRanges(
    const std::vector<ShaderReflection>& reflections) {
    std::map<std::pair<uint32_t, uint32_t>, ShaderPushConstantRange> merged;
    for (const auto& refl : reflections) {
        for (const auto& pc : refl.pushConstantRanges) {
            auto key = std::make_pair(pc.offset, pc.size);
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged[key] = pc;
            } else {
                it->second.stageFlags |= pc.stageFlags;
            }
        }
    }
    std::vector<ShaderPushConstantRange> result;
    result.reserve(merged.size());
    for (auto& [key, range] : merged) {
        result.push_back(range);
    }
    return result;
}

PipelineBuilder::PipelineBuilder(Context& ctx, ShaderLibrary& shaderLib)
    : m_ctx(ctx), m_shaderLib(shaderLib) {}

PipelineBuilder& PipelineBuilder::shader(const std::string& path) {
    m_shaderPaths.push_back(path);
    return *this;
}

PipelineBuilder& PipelineBuilder::renderPass(VkRenderPass rp, uint32_t subpass) {
    m_renderPass = rp;
    m_subpass = subpass;
    return *this;
}

PipelineBuilder& PipelineBuilder::viewport(const VkExtent2D& extent) {
    m_extent = extent;
    return *this;
}

PipelineBuilder& PipelineBuilder::topology(VkPrimitiveTopology t) {
    m_topology = t;
    return *this;
}

PipelineBuilder& PipelineBuilder::cullMode(VkCullModeFlags mode) {
    m_cullMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::frontFace(VkFrontFace face) {
    m_frontFace = face;
    return *this;
}

PipelineBuilder& PipelineBuilder::polygonMode(VkPolygonMode mode) {
    m_polygonMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::lineWidth(float width) {
    m_lineWidth = width;
    return *this;
}

PipelineBuilder& PipelineBuilder::samples(VkSampleCountFlagBits samples) {
    m_samples = samples;
    return *this;
}

void PipelineBuilder::build() {
    if (m_shaderPaths.empty()) {
        fatalError("PipelineBuilder: no shaders specified");
    }
    if (m_renderPass == VK_NULL_HANDLE) {
        fatalError("PipelineBuilder: renderPass not set");
    }

    // Load shaders and collect reflections
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<ShaderReflection> reflections;
    stages.reserve(m_shaderPaths.size());
    reflections.reserve(m_shaderPaths.size());

    for (const auto& path : m_shaderPaths) {
        VkShaderModule module = m_shaderLib.load(path);
        const auto& refl = m_shaderLib.getReflection(path);
        reflections.push_back(refl);

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = refl.stage;
        stageInfo.module = module;
        stageInfo.pName = refl.entryPoint.c_str();
        stages.push_back(stageInfo);
    }

    createDescriptorSetLayout(reflections);
    createPipelineLayout(reflections);
    createGraphicsPipeline(stages);

    spdlog::info("[PipelineBuilder] Pipeline built with {} shader(s), {} descriptor binding(s)",
                 stages.size(),
                 mergeDescriptorBindings(reflections).size());
}

void PipelineBuilder::createDescriptorSetLayout(const std::vector<ShaderReflection>& reflections) {
    auto bindings = mergeDescriptorBindings(reflections);
    if (bindings.empty()) {
        // No descriptor bindings: create an empty layout
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        m_descriptorSetLayout = std::make_unique<DescriptorSetLayout>(m_ctx, info);
        return;
    }

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(bindings.size());
    for (const auto& b : bindings) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = b.binding;
        vkBinding.descriptorType = b.descriptorType;
        vkBinding.descriptorCount = b.count;
        vkBinding.stageFlags = b.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();
    m_descriptorSetLayout = std::make_unique<DescriptorSetLayout>(m_ctx, info);
}

void PipelineBuilder::createPipelineLayout(const std::vector<ShaderReflection>& reflections) {
    auto pcRanges = mergePushConstantRanges(reflections);
    std::vector<VkPushConstantRange> vkRanges;
    vkRanges.reserve(pcRanges.size());
    for (const auto& pc : pcRanges) {
        vkRanges.push_back({pc.stageFlags, pc.offset, pc.size});
    }

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout dslHandle = m_descriptorSetLayout->handle();
    info.setLayoutCount = 1;
    info.pSetLayouts = &dslHandle;
    if (!vkRanges.empty()) {
        info.pushConstantRangeCount = static_cast<uint32_t>(vkRanges.size());
        info.pPushConstantRanges = vkRanges.data();
    }
    m_pipelineLayout = std::make_unique<PipelineLayout>(m_ctx, info);
}

void PipelineBuilder::createGraphicsPipeline(const std::vector<VkPipelineShaderStageCreateInfo>& stages) {
    // Vertex Input: auto-generated from vertex shader reflection
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    uint32_t stride = 0;

    for (const auto& path : m_shaderPaths) {
        const auto& refl = m_shaderLib.getReflection(path);
        if (refl.stage != VK_SHADER_STAGE_VERTEX_BIT) continue;

        attributeDescriptions.reserve(refl.vertexInputs.size());
        for (const auto& input : refl.vertexInputs) {
            VkVertexInputAttributeDescription desc{};
            desc.binding = 0;
            desc.location = input.location;
            desc.format = input.format;
            desc.offset = stride;
            attributeDescriptions.push_back(desc);
            stride += formatSize(input.format);
        }
        break; // Only one vertex shader expected
    }
    bindingDescription.stride = stride;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = stride > 0 ? 1 : 0;
    vertexInputInfo.pVertexBindingDescriptions = stride > 0 ? &bindingDescription : nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_extent.width);
    viewport.height = static_cast<float>(m_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = m_polygonMode;
    rasterizer.lineWidth = m_lineWidth;
    rasterizer.cullMode = m_cullMode;
    rasterizer.frontFace = m_frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisample
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_samples;

    // Color Blend
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state (optional, for viewport/scissor if we want to set them at cmd time)
    // Currently static for simplicity

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout->handle();
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = m_subpass;

    m_graphicsPipeline = std::make_unique<GraphicsPipeline>(m_ctx, pipelineInfo);
}

// Release methods
std::unique_ptr<GraphicsPipeline> PipelineBuilder::releasePipeline() {
    return std::move(m_graphicsPipeline);
}

std::unique_ptr<PipelineLayout> PipelineBuilder::releaseLayout() {
    return std::move(m_pipelineLayout);
}

std::unique_ptr<DescriptorSetLayout> PipelineBuilder::releaseDescriptorSetLayout() {
    return std::move(m_descriptorSetLayout);
}

VkPipeline PipelineBuilder::pipelineHandle() const {
    return m_graphicsPipeline ? m_graphicsPipeline->handle() : VK_NULL_HANDLE;
}

VkPipelineLayout PipelineBuilder::layoutHandle() const {
    return m_pipelineLayout ? m_pipelineLayout->handle() : VK_NULL_HANDLE;
}

VkDescriptorSetLayout PipelineBuilder::descriptorSetLayoutHandle() const {
    return m_descriptorSetLayout ? m_descriptorSetLayout->handle() : VK_NULL_HANDLE;
}

} // namespace kazu
