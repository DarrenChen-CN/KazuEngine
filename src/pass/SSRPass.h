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
                   RenderGraph::ResourceHandle albedo,
                   RenderGraph::ResourceHandle hiz = RenderGraph::InvalidResource);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    void setDisplayMode(int mode) { m_displayMode = mode; }
    int  displayMode() const { return m_displayMode; }

    void setTraceMode(int mode) { m_traceMode = mode; }
    int  traceMode() const { return m_traceMode; }

    void setThickness(float thickness) { m_thickness = thickness; }
    float thickness() const { return m_thickness; }

    void setMaxDistance(float d) { m_maxDistance = d; }
    float maxDistance() const { return m_maxDistance; }

    void setStride(float s) { m_stride = s; }
    float stride() const { return m_stride; }

    void setStepCount(int c) { m_stepCount = c; }
    int  stepCount() const { return m_stepCount; }

    void setBinarySearchSteps(int steps) { m_binarySearchSteps = steps; }
    int  binarySearchSteps() const { return m_binarySearchSteps; }

    void setJitterEnabled(bool enabled) { m_jitterEnabled = enabled; }
    bool jitterEnabled() const { return m_jitterEnabled; }

    void setHizVisMip(int mip) { m_hizVisMip = mip; }
    int  hizVisMip() const { return m_hizVisMip; }

    // Efficiency metrics for the ImGui panel.  All values are kept per trace
    // mode so switching modes does not mix old and new measurements.
    float lastGpuTimeMs(int mode = -1) const;
    float avgGpuTimeMs(int mode = -1) const;
    float avgSteps(int mode = -1) const;

    RenderGraph::ResourceHandle outputHandle() const { return m_outputHandle; }

private:
    void createPipeline();
    void createDescriptorSets();
    static uint32_t computeMipLevels(uint32_t width, uint32_t height);

    static constexpr uint32_t kRingSize = 2;

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_sceneColorHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle      = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_normalHandle     = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_materialHandle   = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_albedoHandle     = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_hizHandle        = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_outputHandle     = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kRingSize> m_descriptorSets{};
    VkSampler             m_sampler             = VK_NULL_HANDLE;
    VkSampler             m_depthSampler        = VK_NULL_HANDLE; // nearest

    std::unique_ptr<GPUTimer> m_timer;
    std::array<std::unique_ptr<Buffer>, kRingSize> m_stepBuffers;
    uint32_t m_frameCounter = 0;

    static constexpr int kModeCount = 3;
    std::array<float, kModeCount> m_lastGpuMsByMode{};
    std::array<float, kModeCount> m_avgGpuMsByMode{};
    std::array<float, kModeCount> m_avgStepsByMode{};
    std::array<int,   kRingSize>  m_slotTraceMode{}; // mode used when the slot was dispatched

    float m_avgSteps = 0.0f; // most recent slot's value (for backwards compat)
    int   m_hizMaxMip = 0;

    bool  m_enabled = true;
    int   m_displayMode = 0; // 0 = composite, 1 = reflection only, 2 = hit mask, 3 = thickness
    int   m_traceMode = 1;   // 0 = basic fixed-step, 1 = screen-space DDA, 2 = Hi-Z
    float m_thickness = 0.01f;
    float m_maxDistance = 10.0f;
    float m_stride = 30.0f;
    int   m_stepCount = 12;
    int   m_binarySearchSteps = 6;
    bool  m_jitterEnabled = true;
    int   m_hizVisMip = 0;
};

} // namespace kazu
