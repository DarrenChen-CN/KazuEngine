// ============================================================================
// KazuEngine - RHI Layer: ShaderEffect (Implementation)
// ============================================================================

#include "ShaderEffect.h"
#include "../core/Context.h"
#include "../core/PipelineLayout.h"
#include "../core/GraphicsPipeline.h"
#include "../core/Utils.h"
#include <spdlog/spdlog.h>

namespace kazu {

// ============================================================================
// Static cache storage
// ============================================================================

std::unordered_map<ShaderEffect::Key, std::unique_ptr<ShaderEffect>, ShaderEffect::KeyHash>
    ShaderEffect::s_cache;

// ============================================================================
// Key comparison & hash
// ============================================================================

bool ShaderEffect::Key::operator==(const Key& o) const {
    if (shaderPaths != o.shaderPaths) return false;
    return state == o.state;
}

size_t ShaderEffect::KeyHash::operator()(const Key& k) const {
    size_t h = 0;
    for (const auto& path : k.shaderPaths) {
        h ^= std::hash<std::string>{}(path) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    PipelineStateHash ph;
    h ^= ph(k.state) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

// ============================================================================
// Construction
// ============================================================================

ShaderEffect::ShaderEffect(Key key) : m_key(std::move(key)) {}

// ============================================================================
// Accessors
// ============================================================================

VkPipeline ShaderEffect::pipeline() const {
    return m_pipeline ? m_pipeline->handle() : VK_NULL_HANDLE;
}

VkPipelineLayout ShaderEffect::pipelineLayout() const {
    return m_pipelineLayout ? m_pipelineLayout->handle() : VK_NULL_HANDLE;
}

VkDescriptorSetLayout ShaderEffect::descriptorSetLayout() const {
    return m_descriptorSetLayout;
}

// ============================================================================
// Factory: getOrCreate
// ============================================================================

ShaderEffect* ShaderEffect::getOrCreate(Context& ctx,
                                        ShaderLibrary& lib,
                                        DescriptorSetLayoutCache& dslCache,
                                        PipelineCache& pipeCache,
                                        const Key& key) {
    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
        return it->second.get();
    }

    if (key.shaderPaths.empty()) {
        fatalError("ShaderEffect::getOrCreate: no shader paths");
    }

    // Build via PipelineBuilder
    PipelineBuilder builder(ctx, lib, dslCache);
    for (const auto& path : key.shaderPaths) {
        builder.shader(path);
    }

    // Apply all fixed-function states from the key
    const auto& s = key.state;
    if (s.renderPass != VK_NULL_HANDLE) {
        builder.renderPass(s.renderPass, s.subpass);
    }
    builder.topology(s.topology);
    builder.cullMode(s.cullMode);
    builder.frontFace(s.frontFace);
    builder.polygonMode(s.polygonMode);
    builder.lineWidth(s.lineWidth);
    builder.samples(s.samples);
    builder.depthTest(s.depthTest);
    builder.depthWrite(s.depthWrite);
    builder.depthCompareOp(s.depthCompareOp);
    builder.depthClampEnable(s.depthClampEnable);
    builder.rasterizerDiscardEnable(s.rasterizerDiscardEnable);
    builder.depthBiasEnable(s.depthBiasEnable);
    builder.sampleShadingEnable(s.sampleShadingEnable);
    for (const auto& ds : s.dynamicStates) {
        builder.dynamicState(ds);
    }
    for (size_t i = 0; i < s.colorBlendAttachments.size(); ++i) {
        builder.colorBlendAttachment(static_cast<uint32_t>(i), s.colorBlendAttachments[i]);
    }
    if (!s.vertexBindings.empty() && !s.vertexAttributes.empty()) {
        builder.vertexInput(s.vertexBindings[0], s.vertexAttributes);
    }

    auto result = builder.build(pipeCache);

    auto effect = std::make_unique<ShaderEffect>(key);
    effect->m_pipeline = result.pipeline;
    effect->m_pipelineLayout = std::move(result.layout);
    effect->m_descriptorSetLayout = result.descriptorSetLayout;

    auto* ptr = effect.get();
    s_cache.emplace(key, std::move(effect));

    spdlog::info("[ShaderEffect] Created: {} shader(s), cache size = {}",
                 key.shaderPaths.size(), s_cache.size());
    return ptr;
}

} // namespace kazu
