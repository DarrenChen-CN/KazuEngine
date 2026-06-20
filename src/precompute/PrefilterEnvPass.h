// ============================================================================
// KazuEngine - Precompute Layer: Prefiltered Environment Map Pass
//
// Convolves an HDR environment cubemap with a GGX NDF at multiple roughness
// levels (one per mip) for split-sum specular IBL.
// Output is owned by PrecomputeManager as a persistent Texture.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"

namespace kazu {

class PrecomputeManager;
class Texture;

class PrefilterEnvPass : public PrecomputePass {
public:
    PrefilterEnvPass(uint32_t size = 128);
    ~PrefilterEnvPass() override;

    PrefilterEnvPass(const PrefilterEnvPass&) = delete;
    PrefilterEnvPass& operator=(const PrefilterEnvPass&) = delete;

    // PrecomputePass interface
    OutputDesc outputDesc() const override;
    void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) override;
    Texture* outputTexture() const override { return nullptr; }
    void resolveInputs(PrecomputeManager* mgr) override;

    // Pass interface
    const char* name() const override { return "PrefilterEnv"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    static uint32_t mipCountForSize(uint32_t size);

private:
    void createPipeline();
    void createDescriptorSets();

    uint32_t m_size = 128;
    uint32_t m_mipLevels = 1;

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    Texture* m_environmentCube = nullptr;

    RenderGraph::ResourceHandle m_outputHandle = RenderGraph::InvalidResource;
    Image* m_outputImage = nullptr;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkImageView> m_mipArrayViews;
};

} // namespace kazu
