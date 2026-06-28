// ============================================================================
// KazuEngine - Pass Layer: Hierarchical Z-Buffer (Hi-Z) Build
//
// Builds a min-depth mip pyramid from the GBuffer depth texture.
// Each dispatch reduces the previous level by 2x2 block min.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class HiZPass : public Pass {
public:
    HiZPass();
    ~HiZPass() override;

    HiZPass(const HiZPass&) = delete;
    HiZPass& operator=(const HiZPass&) = delete;

    const char* name() const override { return "HiZBuild"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputDepth(RenderGraph::ResourceHandle depth) { m_depthHandle = depth; }
    RenderGraph::ResourceHandle hizHandle() const { return m_hizHandle; }

private:
    void createPipeline();
    void createDescriptorSets();

    static uint32_t computeMipLevels(uint32_t width, uint32_t height);

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_depthHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_hizHandle   = RenderGraph::InvalidResource;

    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkSampler             m_sampler             = VK_NULL_HANDLE;

    uint32_t m_mipLevels = 0;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkImageView>     m_dstViews;
    VkImageView m_depthView = VK_NULL_HANDLE;
};

} // namespace kazu
