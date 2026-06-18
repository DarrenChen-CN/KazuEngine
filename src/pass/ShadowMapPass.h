#pragma once
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu{

class ShaderEffect;

class ShadowMapPass : public Pass {
public:
    ShadowMapPass() = default;
    ~ShadowMapPass() = default;

    const char* name() const override { return "Shadow Map"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    RenderGraph::ResourceHandle shadowMapHandle() const { return m_shadowMapHandle; }

private:
    RHI* m_rhi = nullptr;
    Scene* m_scene = nullptr;
    RenderGraph* m_renderGraph = nullptr;

    RenderGraph::ResourceHandle m_shadowMapHandle = RenderGraph::InvalidResource;
    RenderGraph::PassHandle m_passHandle = 0;

    ShaderEffect* m_effect = nullptr;
    const uint32_t shadowMapSize = 2048;
};

}