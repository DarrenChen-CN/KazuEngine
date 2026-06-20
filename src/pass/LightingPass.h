// ============================================================================
// KazuEngine - Pass Layer: Lighting Pass
//
// Full-screen quad lighting: samples GBuffer Albedo/Normal/Depth,
// writes HDR scene color via RenderGraph.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"
#include "scene/RendererSettings.h"

namespace kazu {

class ShaderEffect;
class Texture;

class LightingPass : public Pass {
public:
    LightingPass();
    ~LightingPass();

    // Pass interface
    const char* name() const override { return "Lighting"; }
    void declare(RHI* rhi, RenderGraph* rg) override;
    void create(const PassCreateContext& ctx) override;

    void setSettings(const LightingSettings& settings) { m_settings = settings; }
    const LightingSettings& settings() const { return m_settings; }

    void setDisplayMode(int mode) { m_settings.debugView = mode; }
    void setShadowBias(float bias) { m_settings.shadowBias = bias; }
    void setPcfSampleCount(int count) { m_settings.pcfSampleCount = count; }
    void setPcfFilterSize(float size) { m_settings.pcfFilterSize = size; }
    void setLightWidth(float width) { m_settings.lightWidth = width; }
    void setUsePCSS(bool use) { m_settings.shadowMode = use ? ShadowMode_PCSS : ShadowMode_PCF; }

    void execute(const PassExecuteContext& ctx) override;

    // Set GBuffer input handles (called before declare)
    void setInputs(RenderGraph::ResourceHandle albedo,
                   RenderGraph::ResourceHandle normal,
                   RenderGraph::ResourceHandle depth,
                   RenderGraph::ResourceHandle material,
                   RenderGraph::ResourceHandle shadowMap = RenderGraph::InvalidResource);

    void setIBL(Texture* irradiance, Texture* prefilter, Texture* brdfLut);
    void setEnvironment(Texture* environmentCube);

    RenderGraph::ResourceHandle sceneColorHandle() const { return m_sceneColorHandle; }

private:
    RHI*   m_rhi   = nullptr;
    Scene* m_scene = nullptr;
    RenderGraph* m_renderGraph = nullptr;
    RenderGraph::PassHandle m_passHandle = 0;

    RenderGraph::ResourceHandle m_albedoHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_normalHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_depthHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_materialHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_shadowMapHandle = RenderGraph::InvalidResource;
    RenderGraph::ResourceHandle m_sceneColorHandle = RenderGraph::InvalidResource;

    ShaderEffect*    m_effect         = nullptr;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet  m_descriptorSet  = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler        m_sampler        = VK_NULL_HANDLE;

    Texture* m_irradianceMap = nullptr;
    Texture* m_prefilterMap  = nullptr;
    Texture* m_brdfLut       = nullptr;
    Texture* m_environmentMap = nullptr;
    bool     m_iblEnabled    = false;
    bool     m_envEnabled    = false;
    std::unique_ptr<Texture> m_dummyIrradiance;
    std::unique_ptr<Texture> m_dummyPrefilter;
    std::unique_ptr<Texture> m_dummyLut;
    std::unique_ptr<Texture> m_dummyEnv;

    LightingSettings m_settings;
};

} // namespace kazu
