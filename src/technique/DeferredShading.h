// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading
//
// Encapsulates the full Deferred Shading pipeline:
//   - GBuffer Pass (MRT: Albedo/Normal/Material + Depth)
//   - Lighting Pass (full-screen quad, samples GBuffer)
//
// RenderGraph drives execution order and barrier derivation.
// Application layer (main.cpp) only calls init() and renderGraph()->execute().
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;

class DeferredShading {
public:
    DeferredShading();
    ~DeferredShading();

    void init(RHI* rhi, Scene* scene, Camera* camera);
    RenderGraph* renderGraph() const { return m_renderGraph.get(); }

    void setDisplayMode(int mode) { m_displayMode = mode; }
    int  displayMode() const { return m_displayMode; }

    void setCurrentImageIndex(uint32_t idx) { m_currentImageIndex = idx; }

private:
    void createGBufferResources();
    void createLightingResources();
    void buildRenderGraph();

    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;
    int    m_displayMode = 0;

    // GBuffer
    VkRenderPass   m_gbufferRenderPass   = VK_NULL_HANDLE;
    VkFramebuffer  m_gbufferFramebuffer  = VK_NULL_HANDLE;
    VkPipeline     m_gbufferPipeline     = VK_NULL_HANDLE;
    VkPipelineLayout m_gbufferPipelineLayout = VK_NULL_HANDLE;

    // Lighting
    VkPipeline     m_lightingPipeline     = VK_NULL_HANDLE;
    VkPipelineLayout m_lightingPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet  m_lightingDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool m_lightingDescriptorPool = VK_NULL_HANDLE;
    VkSampler        m_lightingSampler = VK_NULL_HANDLE;

    VkImageView m_albedoView = VK_NULL_HANDLE;
    VkImageView m_normalView = VK_NULL_HANDLE;
    uint32_t m_currentImageIndex = 0;

    std::unique_ptr<RenderGraph> m_renderGraph;
};

} // namespace kazu
