// ============================================================================
// KazuEngine - Precompute Layer: Irradiance Convolution Pass
//
// Computes a diffuse irradiance cubemap from an environment cubemap.
// Output is owned by PrecomputeManager as a persistent Texture.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"

namespace kazu {

class PrecomputeManager;
class Texture;

class IrradiancePass : public PrecomputePass {
public:
    IrradiancePass();
    ~IrradiancePass() override;

    IrradiancePass(const IrradiancePass&) = delete;
    IrradiancePass& operator=(const IrradiancePass&) = delete;

    void setEnvironmentCube(Texture* texture) { m_environmentCube = texture; }

    // PrecomputePass interface
    OutputDesc outputDesc() const override;
    void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) override;
    Texture* outputTexture() const override { return nullptr; }
    void resolveInputs(PrecomputeManager* mgr) override;

    // Pass interface
    const char* name() const override { return "Irradiance"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

private:
    void createPipeline();
    void createDescriptorSet();

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    Texture* m_environmentCube = nullptr;

    RenderGraph::ResourceHandle m_outputHandle = RenderGraph::InvalidResource;
    Image* m_outputImage = nullptr;

    VkImageView m_outputArrayView = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace kazu
