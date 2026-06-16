// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
//
// Owns RenderGraph, assembles GBufferPass + LightingPass.
// No direct VK object creation — only bridge & state forwarding.
// ============================================================================

#pragma once

#include <memory>
#include "rendergraph/RenderGraph.h"
#include "technique/Technique.h"

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;
class GBufferPass;
class LightingPass;
class PresentPass;

class DeferredShading : public Technique {
public:
    DeferredShading();
    ~DeferredShading();

    const char* name() const override { return "Deferred Shading"; }
    RenderGraph* renderGraph() const { return m_renderGraph.get(); }

    void setDisplayMode(int mode);
    int  displayMode() const { return m_displayMode; }

    void render(const RenderFrameContext& frame) override;

    // Expose GBuffer outputs for downstream passes / external techniques
    RenderGraph::ResourceHandle albedoHandle() const;
    RenderGraph::ResourceHandle normalHandle() const;
    RenderGraph::ResourceHandle materialHandle() const;
    RenderGraph::ResourceHandle depthHandle() const;

    // Build panel description for AppUI (zero ImGui dependency)
    void exposePanel(PanelDesc& desc) override;
    bool onKey(int key, int scancode, int action, int mods) override;

private:
    void onInit() override;

    int    m_displayMode = 0;

    std::unique_ptr<GBufferPass>  m_gbufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<PresentPass>  m_presentPass;
    std::unique_ptr<RenderGraph>  m_renderGraph;

    RenderGraph::ResourceHandle m_swapchainHandle = RenderGraph::InvalidResource;
};

} // namespace kazu
