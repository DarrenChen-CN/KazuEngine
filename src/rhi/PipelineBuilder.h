// ============================================================================
// KazuEngine - RHI Layer: PipelineBuilder
//
// High-level builder for graphics pipelines. Consumes ShaderLibrary reflection
// data to auto-generate VertexInputState, DescriptorSetLayout, and
// PipelineLayout. Exposes chainable overrides for fixed-function states.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/GraphicsPipeline.h"
#include "../core/PipelineLayout.h"
#include "../core/DescriptorSetLayout.h"
#include "ShaderLibrary.h"
#include <vector>
#include <string>
#include <memory>

namespace kazu {

class PipelineBuilder {
public:
    PipelineBuilder(Context& ctx, ShaderLibrary& shaderLib);

    // Shader specification (paths, stage auto-detected from SPIR-V reflection)
    PipelineBuilder& shader(const std::string& path);

    // Render target
    PipelineBuilder& renderPass(VkRenderPass rp, uint32_t subpass = 0);
    PipelineBuilder& viewport(const VkExtent2D& extent);

    // Fixed-function state overrides (sensible defaults if not called)
    PipelineBuilder& topology(VkPrimitiveTopology t);
    PipelineBuilder& cullMode(VkCullModeFlags mode);
    PipelineBuilder& frontFace(VkFrontFace face);
    PipelineBuilder& polygonMode(VkPolygonMode mode);
    PipelineBuilder& lineWidth(float width);
    PipelineBuilder& samples(VkSampleCountFlagBits samples);

    // Build: creates DescriptorSetLayout + PipelineLayout + GraphicsPipeline
    void build();

    // Take ownership of built objects (std::move)
    std::unique_ptr<GraphicsPipeline> releasePipeline();
    std::unique_ptr<PipelineLayout> releaseLayout();
    std::unique_ptr<DescriptorSetLayout> releaseDescriptorSetLayout();

    // Peek handles without releasing
    VkPipeline pipelineHandle() const;
    VkPipelineLayout layoutHandle() const;
    VkDescriptorSetLayout descriptorSetLayoutHandle() const;

private:
    Context& m_ctx;
    ShaderLibrary& m_shaderLib;

    std::vector<std::string> m_shaderPaths;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    uint32_t m_subpass = 0;
    VkExtent2D m_extent{0, 0};

    // Fixed-function states
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags m_cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace m_frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPolygonMode m_polygonMode = VK_POLYGON_MODE_FILL;
    float m_lineWidth = 1.0f;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;

    // Built objects
    std::unique_ptr<DescriptorSetLayout> m_descriptorSetLayout;
    std::unique_ptr<PipelineLayout> m_pipelineLayout;
    std::unique_ptr<GraphicsPipeline> m_graphicsPipeline;

    // Helpers
    void createDescriptorSetLayout(const std::vector<ShaderReflection>& reflections);
    void createPipelineLayout(const std::vector<ShaderReflection>& reflections);
    void createGraphicsPipeline(const std::vector<VkPipelineShaderStageCreateInfo>& stages);
};

} // namespace kazu
