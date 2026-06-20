// ============================================================================
// KazuEngine - Pass Layer: FXAA Pass
//
// LDR post-process anti-aliasing. Reads SceneColorLDR, writes SceneColorPost.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;

class FXAAPass : public Pass {
public:
    FXAAPass();
    ~FXAAPass();

    const char* name() const override { return "FXAA"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInput(RenderGraph::ResourceHandle ldr) { m_inputLDRHandle = ldr; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    RenderGraph::ResourceHandle outputHandle() const { return m_outputHandle; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputLDRHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_outputHandle = RenderGraph::InvalidResource;

    VkPipeline        m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout  m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet   m_descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool  m_descriptorPool = VK_NULL_HANDLE;
    VkSampler         m_sampler = VK_NULL_HANDLE;

    bool m_enabled = true;
};

} // namespace kazu
