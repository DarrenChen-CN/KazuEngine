// ============================================================================
// KazuEngine - Pass Layer: Screen Space Reflection (SSR)
//
// Phase 1: basic fixed-step ray marching in screen space.
// Reads GBuffer depth/normal/material and the HDR scene color, outputs
// SceneColorHDR blended with local reflections.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <array>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"
#include "core/GPUTimer.h"
#include "core/Buffer.h"

namespace kazu {

class SSRPass : public Pass {
public:
    SSRPass();
    ~SSRPass() override;

    SSRPass(const SSRPass&) = delete;
    SSRPass& operator=(const SSRPass&) = delete;

    const char* name() const override { return "SSR"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputs(RenderGraph::ResourceHandle sceneColor,
                   RenderGraph::ResourceHandle depth,
                   RenderGraph::ResourceHandle normal,
                   RenderGraph::ResourceHandle material,
                   RenderGraph::ResourceHandle hiz = RenderGraph::InvalidResource);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    void setDisplayMode(int mode) { m_displayMode = mode; }
    int  displayMode() const { return m_displayMode; }

    void setTraceMode(int mode) { m_traceMode = mode; }
    int  traceMode() const { return m_traceMode; }

    // Efficiency metrics for the ImGui panel.
    float lastGpuTimeMs() const { return m_lastGpuMs; }
    float avgGpuTimeMs() const { return m_avgGpuMs; }
    float avgSteps() const { return m_avgSteps; }

    RenderGraph::ResourceHandle outputHandle() const { return m_outputHandle; }

private:
    void createPipeline();
    void createDescriptorSets();

    static constexpr uint32_t kRingSize = 2;

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_sceneColorHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle      = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_normalHandle     = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_materialHandle   = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_hizHandle        = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_outputHandle     = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kRingSize> m_descriptorSets{};
    VkSampler             m_sampler             = VK_NULL_HANDLE;

    std::unique_ptr<GPUTimer> m_timer;
    std::array<std::unique_ptr<Buffer>, kRingSize> m_stepBuffers;
    uint32_t m_frameCounter = 0;

    float m_lastGpuMs = 0.0f;
    float m_avgGpuMs = 0.0f;
    float m_avgSteps = 0.0f;

    bool m_enabled = true;
    int  m_displayMode = 0; // 0 = composite, 1 = reflection only, 2 = hit mask
    int  m_traceMode = 1;   // 0 = basic fixed-step, 1 = fixed-step + binary, 2 = Hi-Z
};

} // namespace kazu
