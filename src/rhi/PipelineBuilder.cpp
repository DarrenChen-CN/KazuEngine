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

PipelineBuilder::PipelineBuilder(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache)
    : m_ctx(ctx), m_shaderLib(shaderLib), m_dslCache(dslCache) {}

PipelineBuilder& PipelineBuilder::shader(const std::string& path) {
    m_shaderPaths.push_back(path);
    return *this;
}

PipelineBuilder& PipelineBuilder::renderPass(VkRenderPass rp, uint32_t subpass) {
    m_renderPass = rp;
    m_subpass = subpass;
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

PipelineBuilder& PipelineBuilder::depthTest(bool enable) {
    m_depthTest = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::depthWrite(bool enable) {
    m_depthWrite = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::depthCompareOp(VkCompareOp op) {
    m_depthCompareOp = op;
    return *this;
}

PipelineBuilder& PipelineBuilder::depthClampEnable(bool enable) {
    m_depthClampEnable = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::rasterizerDiscardEnable(bool enable) {
    m_rasterizerDiscardEnable = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::depthBiasEnable(bool enable) {
    m_depthBiasEnable = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::sampleShadingEnable(bool enable) {
    m_sampleShadingEnable = enable;
    return *this;
}

PipelineBuilder& PipelineBuilder::colorBlendAttachment(uint32_t index, const ColorBlendAttachment& config) {
    if (index >= m_colorBlendAttachments.size()) {
        m_colorBlendAttachments.resize(index + 1);
    }
    m_colorBlendAttachments[index] = config;
    return *this;
}

PipelineBuilder& PipelineBuilder::dynamicState(VkDynamicState state) {
    m_dynamicStates.push_back(state);
    return *this;
}

PipelineBuilder& PipelineBuilder::clearDynamicStates() {
    m_dynamicStates.clear();
    return *this;
}

PipelineBuilder& PipelineBuilder::vertexInput(
    const VkVertexInputBindingDescription& binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {
    m_explicitVertexBindings = {binding};
    m_explicitVertexAttributes = attributes;
    return *this;
}

PipelineBuildResult PipelineBuilder::build(PipelineCache& cache) {
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

    // Generate descriptor bindings (merged across stages)
    auto mergedBindings = mergeDescriptorBindings(reflections);
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(mergedBindings.size());
    for (const auto& b : mergedBindings) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = b.binding;
        vkBinding.descriptorType = b.descriptorType;
        vkBinding.descriptorCount = b.count;
        vkBinding.stageFlags = b.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
    }

    // Get or create DescriptorSetLayout via cache
    VkDescriptorSetLayout dslHandle = m_dslCache.getOrCreate(vkBindings);

    // Create PipelineLayout
    auto pcRanges = mergePushConstantRanges(reflections);
    std::vector<VkPushConstantRange> vkRanges;
    vkRanges.reserve(pcRanges.size());
    for (const auto& pc : pcRanges) {
        vkRanges.push_back({pc.stageFlags, pc.offset, pc.size});
    }

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &dslHandle;
    if (!vkRanges.empty()) {
        plInfo.pushConstantRangeCount = static_cast<uint32_t>(vkRanges.size());
        plInfo.pPushConstantRanges = vkRanges.data();
    }
    auto pipelineLayout = std::make_unique<PipelineLayout>(m_ctx, plInfo);

    // Vertex input: prefer explicit layout, fall back to SPIR-V reflection
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    if (!m_explicitVertexBindings.empty()) {
        vertexBindings = m_explicitVertexBindings;
        vertexAttributes = m_explicitVertexAttributes;
    } else {
        uint32_t stride = 0;
        for (const auto& path : m_shaderPaths) {
            const auto& refl = m_shaderLib.getReflection(path);
            if (refl.stage != VK_SHADER_STAGE_VERTEX_BIT) continue;

            vertexAttributes.reserve(refl.vertexInputs.size());
            for (const auto& input : refl.vertexInputs) {
                VkVertexInputAttributeDescription desc{};
                desc.binding = 0;
                desc.location = input.location;
                desc.format = input.format;
                desc.offset = stride;
                vertexAttributes.push_back(desc);
                stride += formatSize(input.format);
            }
            break;
        }

        if (stride > 0) {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = stride;
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            vertexBindings.push_back(binding);
        }
    }

    // Assemble PipelineState (used as cache key)
    PipelineState state{};
    state.shaderPaths = m_shaderPaths;
    state.renderPass = m_renderPass;
    state.subpass = m_subpass;
    state.topology = m_topology;
    state.cullMode = m_cullMode;
    state.frontFace = m_frontFace;
    state.polygonMode = m_polygonMode;
    state.lineWidth = m_lineWidth;
    state.depthClampEnable = m_depthClampEnable;
    state.rasterizerDiscardEnable = m_rasterizerDiscardEnable;
    state.depthBiasEnable = m_depthBiasEnable;
    state.samples = m_samples;
    state.sampleShadingEnable = m_sampleShadingEnable;
    state.depthTest = m_depthTest;
    state.depthWrite = m_depthWrite;
    state.depthCompareOp = m_depthCompareOp;
    state.colorBlendAttachments = m_colorBlendAttachments;
    state.dynamicStates = m_dynamicStates;
    state.vertexBindings = vertexBindings;
    state.vertexAttributes = vertexAttributes;
    state.descriptorSetLayout = dslHandle;
    state.pipelineLayout = pipelineLayout->handle();

    // Check cache
    auto* cached = cache.find(state);
    if (cached) {
        spdlog::info("[PipelineBuilder] Pipeline cache hit ({} shader(s))", m_shaderPaths.size());
        return {cached, std::move(pipelineLayout), dslHandle};
    }

    // Cache miss: create GraphicsPipeline
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings.empty() ? nullptr : vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.empty() ? nullptr : vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state: empty because we use dynamic state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = m_depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = m_rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
    rasterizer.polygonMode = m_polygonMode;
    rasterizer.lineWidth = m_lineWidth;
    rasterizer.cullMode = m_cullMode;
    rasterizer.frontFace = m_frontFace;
    rasterizer.depthBiasEnable = m_depthBiasEnable ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = m_sampleShadingEnable ? VK_TRUE : VK_FALSE;
    multisampling.rasterizationSamples = m_samples;

    // Determine color attachment count from fragment shader reflection (MRT support)
    uint32_t colorAttachmentCount = 1;
    for (const auto& path : m_shaderPaths) {
        const auto& refl = m_shaderLib.getReflection(path);
        if (refl.stage == VK_SHADER_STAGE_FRAGMENT_BIT && refl.outputAttachmentCount > 0) {
            colorAttachmentCount = refl.outputAttachmentCount;
            break;
        }
    }

    std::vector<VkPipelineColorBlendAttachmentState> vkColorBlendAttachments(colorAttachmentCount);
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        const auto& src = (i < m_colorBlendAttachments.size()) ? m_colorBlendAttachments[i] : ColorBlendAttachment{};
        auto& dst = vkColorBlendAttachments[i];
        dst.blendEnable = src.blendEnable ? VK_TRUE : VK_FALSE;
        dst.srcColorBlendFactor = src.srcColorBlendFactor;
        dst.dstColorBlendFactor = src.dstColorBlendFactor;
        dst.colorBlendOp = src.colorBlendOp;
        dst.srcAlphaBlendFactor = src.srcAlphaBlendFactor;
        dst.dstAlphaBlendFactor = src.dstAlphaBlendFactor;
        dst.alphaBlendOp = src.alphaBlendOp;
        dst.colorWriteMask = src.colorWriteMask;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorAttachmentCount;
    colorBlending.pAttachments = vkColorBlendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = m_depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(m_dynamicStates.size());
    dynamicState.pDynamicStates = m_dynamicStates.empty() ? nullptr : m_dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout->handle();
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = m_subpass;

    auto pipeline = std::make_unique<GraphicsPipeline>(m_ctx, pipelineInfo);
    auto* pipelinePtr = pipeline.get();
    cache.insert(state, std::move(pipeline));

    spdlog::info("[PipelineBuilder] Pipeline created and cached ({} shader(s), {} bindings)",
                 stages.size(), mergedBindings.size());

    return {pipelinePtr, std::move(pipelineLayout), dslHandle};
}

} // namespace kazu


