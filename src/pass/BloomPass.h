// ============================================================================
// KazuEngine - Pass Layer: Bloom Pass
//
// HDR bloom post-process. Extracts bright pixels from the HDR scene color,
// downsamples through a mip chain, upsamples back, and stores the blurred
// bloom contribution in mip level 0 of a transient texture.
// Tone mapping is expected to add this contribution back to the HDR color.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

class RHI;
class Image;

class BloomPass : public Pass {
public:
    BloomPass();
    ~BloomPass();

    const char* name() const override { return "Bloom"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;
    void execute(const PassExecuteContext& ctx) override;

    void setInputHDR(RenderGraph::ResourceHandle hdr) { m_inputHDRHandle = hdr; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setThreshold(float threshold) { m_threshold = threshold; }
    void setIntensity(float intensity) { m_intensity = intensity; }
    void setUpsampleRadius(float radius) { m_upsampleRadius = radius; }

    RenderGraph::ResourceHandle outputHandle() const { return m_bloomHandle; }

private:
    void createPipelines();
    void createDescriptorSets();

    void insertComputeBarrier(VkCommandBuffer cmd) const;

    uint32_t computeMipLevels(uint32_t width, uint32_t height) const;

    RHI* m_rhi = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_inputHDRHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_bloomHandle = RenderGraph::InvalidResource;

    Image* m_bloomImage = nullptr;
    uint32_t m_mipLevels = 1;

    // Per-mip views of the bloom image.
    std::vector<VkImageView> m_mipViews;

    // One pipeline per shader stage.
    VkPipeline m_thresholdPipeline = VK_NULL_HANDLE;
    VkPipeline m_downsamplePipeline = VK_NULL_HANDLE;
    VkPipeline m_upsamplePipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    float m_threshold = 1.0f;
    float m_intensity = 0.1f;
    float m_upsampleRadius = 1.0f;
    bool m_enabled = true;
};

} // namespace kazu
