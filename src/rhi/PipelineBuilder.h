// ============================================================================
// KazuEngine - RHI Layer: PipelineBuilder
//
// High-level builder for graphics pipelines. Consumes ShaderLibrary reflection
// data to auto-generate VertexInputState and DescriptorSetLayout.
// Integrates with DescriptorSetLayoutCache and PipelineCache for deduplication.
//
// Uses VK_DYNAMIC_STATE_VIEWPORT / SCISSOR so the same pipeline works across
// different swapchain sizes without re-creation.
// ============================================================================

#pragma once

#include "PipelineState.h"
#include "PipelineCache.h"
#include "DescriptorSetLayoutCache.h"
#include "../core/Context.h"
#include "../core/GraphicsPipeline.h"
#include "../core/PipelineLayout.h"
#include "ShaderLibrary.h"
#include <vector>
#include <string>
#include <memory>

namespace kazu {

struct PipelineBuildResult {
    GraphicsPipeline* pipeline = nullptr;          // borrowed from PipelineCache
    std::unique_ptr<PipelineLayout> layout;        // owned by caller
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE; // from DescriptorSetLayoutCache
};

class PipelineBuilder {
public:
    PipelineBuilder(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache);

    // Shader specification (paths, stage auto-detected from SPIR-V reflection)
    PipelineBuilder& shader(const std::string& path);

    // Render target
    PipelineBuilder& renderPass(VkRenderPass rp, uint32_t subpass = 0);

    // Fixed-function state overrides (sensible defaults if not called)
    PipelineBuilder& topology(VkPrimitiveTopology t);
    PipelineBuilder& cullMode(VkCullModeFlags mode);
    PipelineBuilder& frontFace(VkFrontFace face);
    PipelineBuilder& polygonMode(VkPolygonMode mode);
    PipelineBuilder& lineWidth(float width);
    PipelineBuilder& samples(VkSampleCountFlagBits samples);

    // Build: checks PipelineCache first; on miss creates DSL (via cache) + PL + GP
    PipelineBuildResult build(PipelineCache& cache);

private:
    Context& m_ctx;
    ShaderLibrary& m_shaderLib;
    DescriptorSetLayoutCache& m_dslCache;

    std::vector<std::string> m_shaderPaths;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    uint32_t m_subpass = 0;

    // Fixed-function states
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags m_cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace m_frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPolygonMode m_polygonMode = VK_POLYGON_MODE_FILL;
    float m_lineWidth = 1.0f;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
};

} // namespace kazu
