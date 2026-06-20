// ============================================================================
// KazuEngine - Precompute Layer: Equirectangular to Cubemap Pass
//
// Converts an HDR equirectangular 2D texture into a cubemap.
// Output is owned by PrecomputeManager as a persistent Texture.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"

namespace kazu {

class Texture;

class EquirectToCubePass : public PrecomputePass {
public:
    explicit EquirectToCubePass(Texture* equirectTexture, uint32_t size = 512);
    ~EquirectToCubePass() override;

    EquirectToCubePass(const EquirectToCubePass&) = delete;
    EquirectToCubePass& operator=(const EquirectToCubePass&) = delete;

    // PrecomputePass interface
    OutputDesc outputDesc() const override;
    void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) override;
    Texture* outputTexture() const override { return nullptr; }

    // Pass interface
    const char* name() const override { return "EquirectToCube"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    Texture* m_equirectTexture = nullptr;

    RenderGraph::ResourceHandle m_outputHandle = RenderGraph::InvalidResource;
    Image* m_outputImage = nullptr;
    uint32_t m_size = 512;

    VkImageView m_outputArrayView = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace kazu
