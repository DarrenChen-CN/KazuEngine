// ============================================================================
// KazuEngine - Pass Layer: SSR Horizontal Blur Pass
//
// First pass of a separable Gaussian blur of the SSR reflection contribution.
// Reads SSRReflect, writes SSRBlurredTemp.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;

class SSRBlurHPass : public Pass {
public:
    SSRBlurHPass();
    ~SSRBlurHPass() override;

    SSRBlurHPass(const SSRBlurHPass&) = delete;
    SSRBlurHPass& operator=(const SSRBlurHPass&) = delete;

    const char* name() const override { return "SSRBlurH"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputSSR(RenderGraph::ResourceHandle ssr) { m_inputSSRHandle = ssr; }
    void setRadius(float radius) { m_radius = radius; }

    RenderGraph::ResourceHandle blurredTempHandle() const { return m_blurredTempHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    struct PushConstants {
        float screenSize[2];
        float radius;
    };

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputSSRHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_blurredTempHandle = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
    VkSampler             m_sampler             = VK_NULL_HANDLE;

    float m_radius = 1.0f;
};

} // namespace kazu
