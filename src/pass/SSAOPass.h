// ============================================================================
// KazuEngine - Pass Layer: Screen-Space Ambient Occlusion (SSAO)
//
// Computes a per-pixel occlusion factor from GBuffer depth + normal.
// Output: R8_UNORM AO texture (white = fully lit, black = occluded).
// ============================================================================

#pragma once

#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class SSAOPass : public Pass {
public:
    SSAOPass();
    ~SSAOPass() override;

    SSAOPass(const SSAOPass&) = delete;
    SSAOPass& operator=(const SSAOPass&) = delete;

    const char* name() const override { return "SSAO"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputs(RenderGraph::ResourceHandle normal,
                   RenderGraph::ResourceHandle depth);

    RenderGraph::ResourceHandle aoHandle() const { return m_aoHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_normalHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle  = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_aoHandle     = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
    VkSampler             m_sampler             = VK_NULL_HANDLE;
};

} // namespace kazu
