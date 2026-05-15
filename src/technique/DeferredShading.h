// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Composer)
//
// Pure composer: owns RenderGraph, assembles GBufferPass + LightingPass.
// No direct VK object creation — only bridge & state forwarding.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;
class PipelineCache;
class PipelineLayout;
class GBufferPass;

class DeferredShading {
public:
    DeferredShading();
    ~DeferredShading();

    void init(RHI* rhi, Scene* scene, Camera* camera);
    RenderGraph* renderGraph() const { return m_renderGraph.get(); }

    void setDisplayMode(int mode) { m_displayMode = mode; }
    int  displayMode() const { return m_displayMode; }

    void setCurrentImageIndex(uint32_t idx) { m_currentImageIndex = idx; }

    // Expose GBuffer outputs for downstream passes / external techniques
    RenderGraph::ResourceHandle albedoHandle() const;
    RenderGraph::ResourceHandle normalHandle() const;
    RenderGraph::ResourceHandle materialHandle() const;
    RenderGraph::ResourceHandle depthHandle() const;

private:
    void buildLightingPipelineAndDescriptors();

    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;
    int    m_displayMode = 0;
    uint32_t m_currentImageIndex = 0;

    // Passes
    std::unique_ptr<GBufferPass> m_gbufferPass;

    // RenderGraph (shared infrastructure)
    std::unique_ptr<RenderGraph> m_renderGraph;

    // ---- Lighting (to be extracted into LightingPass in next Nano-Feature) ----
    VkPipeline     m_lightingPipeline     = VK_NULL_HANDLE;
    VkPipelineLayout m_lightingPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet  m_lightingDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool m_lightingDescriptorPool = VK_NULL_HANDLE;
    VkSampler        m_lightingSampler = VK_NULL_HANDLE;
    std::unique_ptr<PipelineLayout>  m_lightingPipelineLayoutObj;
    std::unique_ptr<PipelineCache>   m_lightingPipelineCache;
};

} // namespace kazu
