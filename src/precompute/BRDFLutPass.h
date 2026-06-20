// ============================================================================
// KazuEngine - Pass Layer: BRDF LUT Precompute Pass
//
// Generates the split-sum BRDF LUT (scale/bias) used by specular IBL.
// The persistent output image is created and owned by PrecomputeManager.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"
#include <memory>

namespace kazu {

class BRDFLutPass : public PrecomputePass {
public:
    BRDFLutPass();
    ~BRDFLutPass() override;

    BRDFLutPass(const BRDFLutPass&) = delete;
    BRDFLutPass& operator=(const BRDFLutPass&) = delete;

    // PrecomputePass interface
    OutputDesc outputDesc() const override;
    void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) override;
    Texture* outputTexture() const override { return m_outputTexture; }

    // Pass interface
    const char* name() const override { return "BRDFLut"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setSize(uint32_t size) { m_size = size; }

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::ResourceHandle m_lutHandle = RenderGraph::InvalidResource;
    Image* m_outputImage = nullptr;
    Texture* m_outputTexture = nullptr;
    uint32_t m_size = 512;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace kazu
