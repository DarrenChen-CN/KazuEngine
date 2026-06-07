// ============================================================================
// KazuEngine - Pass Layer: GBuffer Pass
//
// Renders scene into MRT: Albedo / Normal / Material + Depth.
// Downstream passes can query output handles via albedoHandle() etc.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class ShaderEffect;

class GBufferPass : public Pass {
public:
    GBufferPass();
    ~GBufferPass();

    // Pass interface
    const char* name() const override { return "GBuffer"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(Scene* scene, Camera* camera, RenderGraph* rg) override;

    // Called by RenderGraph execute lambda
    void execute(VkCommandBuffer cmd);

    // Output handles for downstream passes to reference
    RenderGraph::ResourceHandle albedoHandle() const  { return m_albedoHandle; }
    RenderGraph::ResourceHandle normalHandle() const  { return m_normalHandle; }
    RenderGraph::ResourceHandle materialHandle() const { return m_materialHandle; }
    RenderGraph::ResourceHandle depthHandle() const   { return m_depthHandle; }

    VkRenderPass renderPass() const { return m_renderPass; }
    ShaderEffect* shaderEffect() const { return m_effect; }

private:
    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;

    RenderGraph::ResourceHandle m_albedoHandle  = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_normalHandle  = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_materialHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle   = RenderGraph::InvalidResource;

    VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
    VkFramebuffer    m_framebuffer    = VK_NULL_HANDLE;
    ShaderEffect*    m_effect         = nullptr;
};

} // namespace kazu
