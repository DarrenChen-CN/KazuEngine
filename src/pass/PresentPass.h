// ============================================================================
// KazuEngine - Pass Layer: Present Pass
//
// Samples LDR scene color (output of TonemapPass) and writes to the imported swapchain image.
// AppUI remains outside the RenderGraph and overlays after this pass.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class ShaderEffect;

class PresentPass : public Pass {
public:
    PresentPass();
    ~PresentPass();

    const char* name() const override { return "Present"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;

    void setInput(RenderGraph::ResourceHandle sceneColor) { m_sceneColorHandle = sceneColor; }
    void setSwapchainHandle(RenderGraph::ResourceHandle swapchain) { m_swapchainHandle = swapchain; }

    void execute(const PassExecuteContext& ctx) override;

private:
    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_sceneColorHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_swapchainHandle = RenderGraph::InvalidResource;

    ShaderEffect* m_effect = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace kazu
