// ============================================================================
// KazuEngine - Pass Layer: Light Visualize Pass
//
// Renders unlit instances (e.g. light source markers) on top of the lit scene.
// Does not go through GBuffer/Lighting; writes directly to SceneColorHDR.
// ============================================================================

#pragma once
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class ShaderEffect;

class LightVisualizePass : public Pass {
public:
    LightVisualizePass() = default;
    ~LightVisualizePass();

    const char* name() const override { return "LightVisualize"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInput(RenderGraph::ResourceHandle sceneColor) { m_sceneColorHandle = sceneColor; }

private:
    RHI* m_rhi = nullptr;
    Scene* m_scene = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::ResourceHandle m_sceneColorHandle = RenderGraph::InvalidResource;
    RenderGraph::PassHandle m_passHandle = 0;
    ShaderEffect* m_effect = nullptr;
};

} // namespace kazu
