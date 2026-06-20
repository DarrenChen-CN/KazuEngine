// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
// ============================================================================

#include "technique/DeferredShading.h"
#include "pass/GBufferPass.h"
#include "pass/LightingPass.h"
#include "pass/LightVisualizePass.h"
#include "pass/PresentPass.h"
#include "pass/ShadowMapPass.h"
#include "app/AppUI.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <GLFW/glfw3.h>
#include <vector>

namespace kazu {

DeferredShading::DeferredShading() = default;

DeferredShading::~DeferredShading() {
    if (!m_rhi) return;
    vkDeviceWaitIdle(m_rhi->ctx().device());
}

void DeferredShading::onInit() {
    if (!m_lightingSettingsInitialized) {
        m_lightingSettings = m_scene->rendererSettings().lighting;
        m_lightingSettingsInitialized = true;
    }

    // ---- Phase 1: Declare ----
    m_renderGraph = std::make_unique<RenderGraph>(m_rhi->ctx());

    // Import swapchain as an external resource (handle is rebound per-frame)
    m_swapchainHandle = m_renderGraph->addImportedTexture(
        "Swapchain",
        {.width = m_rhi->extent().width,
         .height = m_rhi->extent().height,
         .format = m_rhi->swapchainFormat(),
         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
        m_rhi->swapchainImage(0),      // placeholder, rebound each frame
        m_rhi->swapchainImageView(0)); // placeholder, rebound each frame
    std::vector<VkImageView> swapchainViews;
    swapchainViews.reserve(m_rhi->swapchainImageCount());
    for (uint32_t i = 0; i < m_rhi->swapchainImageCount(); ++i) {
        swapchainViews.push_back(m_rhi->swapchainImageView(i));
    }
    m_renderGraph->setImportedTextureViews(m_swapchainHandle, swapchainViews);

    m_gbufferPass = std::make_unique<GBufferPass>();
    m_gbufferPass->declare(m_rhi, m_renderGraph.get());

    m_shadowMapPass = std::make_unique<ShadowMapPass>();
    m_shadowMapPass->declare(m_rhi, m_renderGraph.get());

    m_lightingPass = std::make_unique<LightingPass>();
    m_lightingPass->setInputs(
        m_gbufferPass->albedoHandle(),
        m_gbufferPass->normalHandle(),
        m_gbufferPass->depthHandle(),
        m_gbufferPass->materialHandle(),
        m_shadowMapPass->shadowMapHandle());
    m_lightingPass->setIBL(m_iblIrradiance, m_iblPrefilter, m_iblLut);
    m_lightingPass->setEnvironment(m_environmentMap);
    m_lightingPass->declare(m_rhi, m_renderGraph.get());

    m_lightVisualizePass = std::make_unique<LightVisualizePass>();
    m_lightVisualizePass->setInput(m_lightingPass->sceneColorHandle());
    m_lightVisualizePass->declare(m_rhi, m_renderGraph.get());

    m_presentPass = std::make_unique<PresentPass>();
    m_presentPass->setInput(m_lightingPass->sceneColorHandle());
    m_presentPass->setSwapchainHandle(m_swapchainHandle);
    m_presentPass->declare(m_rhi, m_renderGraph.get());

    // ---- Phase 2: Compile ----
    if (!m_renderGraph->compile()) {
        fatalError("RenderGraph compile failed");
    }

    // ---- Phase 3: Create VK objects ----
    PassCreateContext passCtx{};
    passCtx.rhi = m_rhi;
    passCtx.renderGraph = m_renderGraph.get();
    passCtx.scene = m_scene;

    m_gbufferPass->create(passCtx);

    // Build scene materials now that ShaderEffect is ready
    m_scene->buildMaterials(m_rhi->ctx(), m_gbufferPass->shaderEffect(),
                            m_rhi->dslCache());

    m_shadowMapPass->create(passCtx);
    m_lightingPass->create(passCtx);
    m_lightVisualizePass->create(passCtx);
    m_presentPass->create(passCtx);

    // Restore lighting settings after resize re-init.
    m_lightingPass->setSettings(m_lightingSettings);

    spdlog::info("DeferredShading initialized (pure composer)");
}

void DeferredShading::render(const RenderFrameContext& frame) {
    if (!m_renderGraph) return;

    if (m_swapchainHandle != RenderGraph::InvalidResource) {
        m_renderGraph->bindImportedTexture(
            m_swapchainHandle,
            frame.swapchainImage,
            frame.swapchainImageView);
    }

    if (m_lightingPass) {
        m_lightingPass->setSettings(m_lightingSettings);
    }

    PassExecuteContext passCtx{};
    passCtx.cmd = frame.cmd;
    passCtx.imageIndex = frame.imageIndex;
    passCtx.camera = m_camera;
    passCtx.light = &m_scene->directionalLight();
    m_renderGraph->execute(passCtx);
}

void DeferredShading::exposePanel(PanelDesc& desc) {
    desc.name = "Deferred Shading";
    static const char* modes[] = {"Lighting", "Albedo", "Normal", "Shadow Map"};
    PanelItem displayModeItem{};
    displayModeItem.type = PanelItem::Enum;
    displayModeItem.label = "Display Mode";
    displayModeItem.e.value = &m_lightingSettings.debugView;
    displayModeItem.e.names = modes;
    displayModeItem.e.count = 4;
    desc.items.push_back(displayModeItem);

    static const char* lightingModels[] = {"Lambert", "PBR"};
    PanelItem lightingModelItem{};
    lightingModelItem.type = PanelItem::Enum;
    lightingModelItem.label = "Lighting Model";
    lightingModelItem.e.value = &m_lightingSettings.lightingModel;
    lightingModelItem.e.names = lightingModels;
    lightingModelItem.e.count = 2;
    desc.items.push_back(lightingModelItem);

    static const char* shadowModes[] = {"None", "Hard", "PCF", "PCSS", "CSM"};
    PanelItem shadowModeItem{};
    shadowModeItem.type = PanelItem::Enum;
    shadowModeItem.label = "Shadow Mode";
    shadowModeItem.e.value = &m_lightingSettings.shadowMode;
    shadowModeItem.e.names = shadowModes;
    shadowModeItem.e.count = 5;
    desc.items.push_back(shadowModeItem);

    desc.items.push_back({PanelItem::Separator, "", {}});

    PanelItem shadowBiasItem{};
    shadowBiasItem.type = PanelItem::Float;
    shadowBiasItem.label = "Shadow Bias";
    shadowBiasItem.f.value = &m_lightingSettings.shadowBias;
    shadowBiasItem.f.min = 0.0f;
    shadowBiasItem.f.max = 0.05f;
    desc.items.push_back(shadowBiasItem);

    PanelItem pcfCountItem{};
    pcfCountItem.type = PanelItem::Int;
    pcfCountItem.label = "PCF Samples";
    pcfCountItem.i.value = &m_lightingSettings.pcfSampleCount;
    pcfCountItem.i.min = 1;
    pcfCountItem.i.max = 64;
    desc.items.push_back(pcfCountItem);

    PanelItem pcfSizeItem{};
    pcfSizeItem.type = PanelItem::Float;
    pcfSizeItem.label = "PCF Filter Size";
    pcfSizeItem.f.value = &m_lightingSettings.pcfFilterSize;
    pcfSizeItem.f.min = 0.0f;
    pcfSizeItem.f.max = 0.05f;
    desc.items.push_back(pcfSizeItem);

    PanelItem lightWidthItem{};
    lightWidthItem.type = PanelItem::Float;
    lightWidthItem.label = "Light Width";
    lightWidthItem.f.value = &m_lightingSettings.lightWidth;
    lightWidthItem.f.min = 0.0f;
    lightWidthItem.f.max = 0.2f;
    desc.items.push_back(lightWidthItem);
}

void DeferredShading::setDisplayMode(int mode) {
    m_lightingSettings.debugView = mode;
    if (m_lightingPass) m_lightingPass->setDisplayMode(mode);
}

void DeferredShading::setShadowBias(float bias) {
    m_lightingSettings.shadowBias = bias;
    if (m_lightingPass) m_lightingPass->setShadowBias(bias);
}

void DeferredShading::setPcfSampleCount(int count) {
    m_lightingSettings.pcfSampleCount = glm::clamp(count, 1, 64);
    if (m_lightingPass) m_lightingPass->setPcfSampleCount(m_lightingSettings.pcfSampleCount);
}

void DeferredShading::setPcfFilterSize(float size) {
    m_lightingSettings.pcfFilterSize = size;
    if (m_lightingPass) m_lightingPass->setPcfFilterSize(m_lightingSettings.pcfFilterSize);
}

void DeferredShading::setLightWidth(float width) {
    m_lightingSettings.lightWidth = width;
    if (m_lightingPass) m_lightingPass->setLightWidth(m_lightingSettings.lightWidth);
}

void DeferredShading::setUsePCSS(bool use) {
    m_lightingSettings.shadowMode = use ? ShadowMode_PCSS : ShadowMode_PCF;
    if (m_lightingPass) m_lightingPass->setUsePCSS(use);
}

void DeferredShading::setIBL(Texture* irradiance, Texture* prefilter, Texture* brdfLut) {
    m_iblIrradiance = irradiance;
    m_iblPrefilter = prefilter;
    m_iblLut = brdfLut;
    if (m_lightingPass) {
        m_lightingPass->setIBL(irradiance, prefilter, brdfLut);
    }
}

void DeferredShading::setEnvironment(Texture* environmentCube) {
    m_environmentMap = environmentCube;
    if (m_lightingPass) {
        m_lightingPass->setEnvironment(environmentCube);
    }
}

bool DeferredShading::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key != GLFW_KEY_D || action != GLFW_PRESS) return false;

    int mode = (displayMode() + 1) % 4;
    setDisplayMode(mode);
    const char* modeName = (mode == 0) ? "lighting" : (mode == 1) ? "albedo" : (mode == 2) ? "normal" : "shadow map";
    spdlog::info("Display mode: {}", modeName);
    return true;
}

RenderGraph::ResourceHandle DeferredShading::albedoHandle() const {
    return m_gbufferPass ? m_gbufferPass->albedoHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::normalHandle() const {
    return m_gbufferPass ? m_gbufferPass->normalHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::materialHandle() const {
    return m_gbufferPass ? m_gbufferPass->materialHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::depthHandle() const {
    return m_gbufferPass ? m_gbufferPass->depthHandle() : RenderGraph::InvalidResource;
}

} // namespace kazu
