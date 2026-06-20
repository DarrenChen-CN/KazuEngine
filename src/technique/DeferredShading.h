// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
//
// Owns RenderGraph, assembles GBufferPass + LightingPass.
// No direct VK object creation — only bridge & state forwarding.
// ============================================================================

#pragma once

#include <memory>
#include "pass/LightingPass.h"
#include "rendergraph/RenderGraph.h"
#include "technique/Technique.h"

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;
class GBufferPass;
class PresentPass;
class ShadowMapPass;
class LightVisualizePass;
class Texture;

class DeferredShading : public Technique {
public:
    DeferredShading();
    ~DeferredShading();

    const char* name() const override { return "Deferred Shading"; }
    RenderGraph* renderGraph() const { return m_renderGraph.get(); }

    void setDisplayMode(int mode);
    int  displayMode() const { return m_lightingSettings.debugView; }
    void setShadowBias(float bias);
    float shadowBias() const { return m_lightingSettings.shadowBias; }
    void setPcfSampleCount(int count);
    int  pcfSampleCount() const { return m_lightingSettings.pcfSampleCount; }
    void setPcfFilterSize(float size);
    float pcfFilterSize() const { return m_lightingSettings.pcfFilterSize; }
    void setLightWidth(float width);
    float lightWidth() const { return m_lightingSettings.lightWidth; }
    void setUsePCSS(bool use);
    bool usePCSS() const { return m_lightingSettings.shadowMode == ShadowMode_PCSS; }

    void setIBL(Texture* irradiance, Texture* prefilter, Texture* brdfLut) override;
    void setEnvironment(Texture* environmentCube) override;

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

    LightingSettings m_lightingSettings;

    std::unique_ptr<GBufferPass>  m_gbufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<LightVisualizePass> m_lightVisualizePass;
    std::unique_ptr<PresentPass>  m_presentPass;
    std::unique_ptr<ShadowMapPass> m_shadowMapPass;
    std::unique_ptr<RenderGraph>  m_renderGraph;

    RenderGraph::ResourceHandle m_swapchainHandle = RenderGraph::InvalidResource;
    bool m_lightingSettingsInitialized = false;

    Texture* m_iblIrradiance = nullptr;
    Texture* m_iblPrefilter  = nullptr;
    Texture* m_iblLut        = nullptr;
    Texture* m_environmentMap = nullptr;
};

} // namespace kazu
