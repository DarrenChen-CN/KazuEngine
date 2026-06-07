// ============================================================================
// KazuEngine - Pass Layer: Lighting Pass
//
// Full-screen quad lighting: samples GBuffer Albedo/Normal/Depth,
// writes to imported swapchain image via RenderGraph.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class ShaderEffect;

class LightingPass : public Pass {
public:
    LightingPass();
    ~LightingPass();

    // Pass interface
    const char* name() const override { return "Lighting"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(Scene* scene, Camera* camera, RenderGraph* rg) override;

    // State forwarded from Technique each frame
    void setCurrentImageIndex(uint32_t idx) { m_currentImageIndex = idx; }
    void setDisplayMode(int mode) { m_displayMode = mode; }

    // Set the imported swapchain resource handle (called before declare)
    void setSwapchainHandle(RenderGraph::ResourceHandle handle) {
        m_swapchainHandle = handle;
    }

    // Called by RenderGraph execute lambda
    void execute(VkCommandBuffer cmd);

    // Set GBuffer input handles (called before declare)
    void setInputs(RenderGraph::ResourceHandle albedo,
                   RenderGraph::ResourceHandle normal,
                   RenderGraph::ResourceHandle depth);

private:
    void createRenderPassAndFramebuffers();
    void destroyRenderPassAndFramebuffers();

    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;

    RenderGraph::ResourceHandle m_albedoHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_normalHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_swapchainHandle = RenderGraph::InvalidResource;

    ShaderEffect*    m_effect         = nullptr;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet  m_descriptorSet  = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler        m_sampler        = VK_NULL_HANDLE;

    VkRenderPass  m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    uint32_t m_currentImageIndex = 0;
    int      m_displayMode = 0;
};

} // namespace kazu
