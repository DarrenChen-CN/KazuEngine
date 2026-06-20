// ============================================================================
// KazuEngine - Precompute Layer: Cubemap Tonemap Pass
//
// Tonemaps an HDR cubemap into an LDR 2D array for ImGui debug visualization.
// Output is owned by PrecomputeManager as a persistent Texture.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"
#include <string>

namespace kazu {

class Texture;

class CubemapTonemapPass : public PrecomputePass {
public:
    CubemapTonemapPass(const std::string& inputName,
                       const std::string& outputName,
                       uint32_t size,
                       float exposure,
                       float gamma,
                       uint32_t mipLevel = 0);
    ~CubemapTonemapPass() override;

    CubemapTonemapPass(const CubemapTonemapPass&) = delete;
    CubemapTonemapPass& operator=(const CubemapTonemapPass&) = delete;

    // PrecomputePass interface
    OutputDesc outputDesc() const override;
    void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) override;
    Texture* outputTexture() const override { return nullptr; }
    void resolveInputs(PrecomputeManager* mgr) override;

    // Pass interface
    const char* name() const override { return m_outputName.c_str(); }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

private:
    void createPipeline();
    void createDescriptorSet();

    std::string m_inputName;
    std::string m_outputName;
    uint32_t m_size = 128;
    float m_exposure = 1.0f;
    float m_gamma = 2.2f;
    uint32_t m_mipLevel = 0;

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    Texture* m_inputCube = nullptr;

    RenderGraph::ResourceHandle m_outputHandle = RenderGraph::InvalidResource;
    Image* m_outputImage = nullptr;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace kazu
