// ============================================================================
// KazuEngine - Pass Layer: SSR Composite Pass
//
// Final pass of the separable Gaussian blur + composite.
// Reads the horizontally-blurred SSR contribution and the original HDR scene
// color, performs the vertical blur, and blends reflections back into the scene.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;

class SSRCompositePass : public Pass {
public:
    SSRCompositePass();
    ~SSRCompositePass() override;

    SSRCompositePass(const SSRCompositePass&) = delete;
    SSRCompositePass& operator=(const SSRCompositePass&) = delete;

    const char* name() const override { return "SSRComposite"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputBlurredTemp(RenderGraph::ResourceHandle blurredTemp) { m_blurredTempHandle = blurredTemp; }
    void setInputSceneColor(RenderGraph::ResourceHandle sceneColor) { m_sceneColorHandle = sceneColor; }
    void setRadius(float radius) { m_radius = radius; }
    void setDisplayMode(int mode) { m_displayMode = mode; }

    RenderGraph::ResourceHandle outputHandle() const { return m_outputHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    struct PushConstants {
        float screenSize[2];
        float radius;
        int displayMode;
    };

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_blurredTempHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_sceneColorHandle   = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_outputHandle       = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
    VkSampler             m_sampler             = VK_NULL_HANDLE;

    float m_radius = 1.0f;
    int   m_displayMode = 0;
};

} // namespace kazu
