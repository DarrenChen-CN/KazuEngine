// ============================================================================
// KazuEngine - Pass Layer: GBuffer Pass
//
// Renders scene into MRT: Albedo / Normal / Material + Depth.
// Downstream passes can query output handles via albedoHandle() etc.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include "rendergraph/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class PipelineCache;
class PipelineLayout;

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
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // RAII owners: PipelineLayout must be declared BEFORE PipelineCache
    // (destruction order: Cache first, then Layout — Cache depends on Layout)
    std::unique_ptr<PipelineLayout> m_pipelineLayoutObj;
    std::unique_ptr<PipelineCache>  m_pipelineCache;
};

} // namespace kazu
