// ============================================================================
// KazuEngine - Pass Layer: TAA Pass
//
// Temporal anti-aliasing on HDR scene color before tone mapping.
// Reads current jittered SceneColorHDR + Depth + history buffer,
// writes resolved color into the current history buffer.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;

class TAAPass : public Pass {
public:
    TAAPass();
    ~TAAPass();

    const char* name() const override { return "TAA"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputHDR(RenderGraph::ResourceHandle hdr) { m_inputHDRHandle = hdr; }
    void setInputDepth(RenderGraph::ResourceHandle depth) { m_inputDepthHandle = depth; }
    void setHistoryRead(RenderGraph::ResourceHandle history) { m_historyReadHandle = history; }
    void setHistoryWrite(RenderGraph::ResourceHandle history) { m_historyWriteHandle = history; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setMatrices(const glm::mat4& invViewProj, const glm::mat4& prevViewProj);

    RenderGraph::ResourceHandle outputHandle() const { return m_historyWriteHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputHDRHandle    = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_inputDepthHandle  = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_historyReadHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_historyWriteHandle= RenderGraph::InvalidResource;

    VkPipeline        m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout  m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet   m_descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool  m_descriptorPool = VK_NULL_HANDLE;
    VkSampler         m_sampler = VK_NULL_HANDLE;

    glm::mat4 m_invViewProj{1.0f};
    glm::mat4 m_prevViewProj{1.0f};
    bool      m_enabled = true;
};

} // namespace kazu
