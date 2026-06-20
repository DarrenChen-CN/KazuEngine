// ============================================================================
// KazuEngine - Pass Layer: SSAO Blur Pass
//
// Simple 5x5 Gaussian blur of the raw SSAO texture to remove noise/halo.
// Output: R8_UNORM blurred AO texture.
// ============================================================================

#pragma once

#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class SSAOBlurPass : public Pass {
public:
    SSAOBlurPass();
    ~SSAOBlurPass() override;

    SSAOBlurPass(const SSAOBlurPass&) = delete;
    SSAOBlurPass& operator=(const SSAOBlurPass&) = delete;

    const char* name() const override { return "SSAOBlur"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputAO(RenderGraph::ResourceHandle ao);

    RenderGraph::ResourceHandle blurredAOHandle() const { return m_blurredAOHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputAOHandle  = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_blurredAOHandle = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
    VkSampler             m_sampler             = VK_NULL_HANDLE;
};

} // namespace kazu
