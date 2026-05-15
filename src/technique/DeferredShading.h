// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
//
// Owns RenderGraph, assembles GBufferPass + LightingPass.
// No direct VK object creation — only bridge & state forwarding.
// ============================================================================

#pragma once

#include <memory>
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;
class GBufferPass;
class LightingPass;

class DeferredShading {
public:
    DeferredShading();
    ~DeferredShading();

    void init(RHI* rhi, Scene* scene, Camera* camera);
    RenderGraph* renderGraph() const { return m_renderGraph.get(); }

    void setDisplayMode(int mode);
    int  displayMode() const { return m_displayMode; }

    void setCurrentImageIndex(uint32_t idx);

    // Expose GBuffer outputs for downstream passes / external techniques
    RenderGraph::ResourceHandle albedoHandle() const;
    RenderGraph::ResourceHandle normalHandle() const;
    RenderGraph::ResourceHandle materialHandle() const;
    RenderGraph::ResourceHandle depthHandle() const;

private:
    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;
    int    m_displayMode = 0;
    uint32_t m_currentImageIndex = 0;

    std::unique_ptr<GBufferPass>  m_gbufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<RenderGraph>  m_renderGraph;
};

} // namespace kazu
