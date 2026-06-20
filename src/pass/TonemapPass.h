// ============================================================================
// KazuEngine - Pass Layer: Tone Mapping Pass
//
// Per-frame compute pass that converts HDR scene color into an LDR image
// using exposure + a tone-mapping operator (Reinhard / ACES) and gamma.
// The resulting LDR texture is consumed by PresentPass.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;

class TonemapPass : public Pass {
public:
    TonemapPass();
    ~TonemapPass();

    const char* name() const override { return "Tonemap"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInput(RenderGraph::ResourceHandle hdr) { m_inputHDRHandle = hdr; }
    void setBloomInput(RenderGraph::ResourceHandle bloom) { m_bloomHandle = bloom; }

    void setExposure(float exposure) { m_exposure = exposure; }
    void setGamma(float gamma) { m_gamma = gamma; }
    void setMode(int mode) { m_mode = mode; }
    void setBloomIntensity(float intensity) { m_bloomIntensity = intensity; }

    RenderGraph::ResourceHandle outputHandle() const { return m_outputLDRHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputHDRHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_bloomHandle    = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_outputLDRHandle = RenderGraph::InvalidResource;

    VkPipeline        m_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout  m_pipelineLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet   m_descriptorSet   = VK_NULL_HANDLE;
    VkDescriptorPool  m_descriptorPool  = VK_NULL_HANDLE;
    VkSampler         m_sampler         = VK_NULL_HANDLE;

    float m_exposure = 1.0f;
    float m_gamma    = 2.2f;
    int   m_mode     = 1; // 0 = Reinhard, 1 = ACES
    float m_bloomIntensity = 0.0f;
};

} // namespace kazu
