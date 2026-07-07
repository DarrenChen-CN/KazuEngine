// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
// ============================================================================

#include "technique/DeferredShading.h"
#include "pass/GBufferPass.h"
#include "pass/LightingPass.h"
#include "pass/LightVisualizePass.h"
#include "pass/PresentPass.h"
#include "pass/ShadowMapPass.h"
#include "pass/SSAOPass.h"
#include "pass/SSAOBlurPass.h"
#include "pass/TonemapPass.h"
#include "pass/FXAAPass.h"
#include "pass/TAAPass.h"
#include "pass/SSRPass.h"
#include "pass/SSRBlurHPass.h"
#include "pass/SSRCompositePass.h"
#include "pass/HiZPass.h"
#include "pass/BloomPass.h"
#include "app/AppUI.h"
#include "core/Utils.h"
#include "core/Image.h"
#include "core/CommandBuffer.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <cstdio>

namespace kazu {

namespace {

float halton(int index, int base) {
    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    int i = index;
    while (i > 0) {
        result += f * static_cast<float>(i % base);
        f /= static_cast<float>(base);
        i /= base;
    }
    return result;
}

glm::vec2 halton23(int index) {
    // Offset by 1 to avoid the (0,0) sample.
    return glm::vec2(halton(index + 1, 2), halton(index + 1, 3));
}

glm::vec2 computeTAAJitter(uint32_t frameIndex, const VkExtent2D& extent) {
    glm::vec2 h = halton23(static_cast<int>(frameIndex % 8));
    // Map from [0,1] to [-0.5,0.5] pixels, then to NDC.
    return (h - 0.5f) * 2.0f / glm::vec2(static_cast<float>(extent.width),
                                          static_cast<float>(extent.height));
}

} // anonymous namespace

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
    VkExtent2D swapExtent = m_rhi->swapchainExtent();
    m_swapchainHandle = m_renderGraph->addImportedTexture(
        "Swapchain",
        {.width = swapExtent.width,
         .height = swapExtent.height,
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

    m_ssaoPass = std::make_unique<SSAOPass>();
    m_ssaoPass->setInputs(
        m_gbufferPass->normalHandle(),
        m_gbufferPass->depthHandle());
    m_ssaoPass->declare(m_rhi, m_renderGraph.get());

    m_ssaoBlurPass = std::make_unique<SSAOBlurPass>();
    m_ssaoBlurPass->setInputAO(m_ssaoPass->aoHandle());
    m_ssaoBlurPass->declare(m_rhi, m_renderGraph.get());

    m_lightingPass = std::make_unique<LightingPass>();
    m_lightingPass->setInputs(
        m_gbufferPass->albedoHandle(),
        m_gbufferPass->normalHandle(),
        m_gbufferPass->depthHandle(),
        m_gbufferPass->materialHandle(),
        m_shadowMapPass->shadowMapHandle(),
        m_ssaoBlurPass->blurredAOHandle());
    m_lightingPass->setIBL(m_iblIrradiance, m_iblPrefilter, m_iblLut);
    m_lightingPass->setEnvironment(m_environmentMap);
    m_lightingPass->declare(m_rhi, m_renderGraph.get());

    m_lightVisualizePass = std::make_unique<LightVisualizePass>();
    m_lightVisualizePass->setInput(m_lightingPass->sceneColorHandle());
    m_lightVisualizePass->declare(m_rhi, m_renderGraph.get());

    m_hizPass = std::make_unique<HiZPass>();
    m_hizPass->setInputDepth(m_gbufferPass->depthHandle());
    m_hizPass->declare(m_rhi, m_renderGraph.get());

    m_ssrPass = std::make_unique<SSRPass>();
    m_ssrPass->setInputs(
        m_lightingPass->sceneColorHandle(),
        m_gbufferPass->depthHandle(),
        m_gbufferPass->normalHandle(),
        m_gbufferPass->materialHandle(),
        m_gbufferPass->albedoHandle(),
        m_hizPass->hizHandle());
    m_ssrPass->declare(m_rhi, m_renderGraph.get());

    m_ssrBlurHPass = std::make_unique<SSRBlurHPass>();
    m_ssrBlurHPass->setInputSSR(m_ssrPass->outputHandle());
    m_ssrBlurHPass->setRadius(m_ssrBlurRadius);
    m_ssrBlurHPass->declare(m_rhi, m_renderGraph.get());

    m_ssrCompositePass = std::make_unique<SSRCompositePass>();
    m_ssrCompositePass->setInputBlurredTemp(m_ssrBlurHPass->blurredTempHandle());
    m_ssrCompositePass->setInputSceneColor(m_lightingPass->sceneColorHandle());
    m_ssrCompositePass->setRadius(m_ssrBlurRadius);
    m_ssrCompositePass->setDisplayMode(m_ssrDisplayMode);
    m_ssrCompositePass->declare(m_rhi, m_renderGraph.get());

    // TAA history buffers (persistent, ping-ponged across frames)
    {
        VkExtent2D extent = m_rhi->extent();
        for (int i = 0; i < 2; ++i) {
            m_taaHistoryImages[i] = std::make_unique<Image>(m_rhi->ctx(),
                ImageDesc{
                    VK_IMAGE_TYPE_2D,
                    {extent.width, extent.height, 1},
                    1, 1,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                });
        }

        // Clear to black and leave them in SHADER_READ_ONLY_OPTIMAL so the
        // RenderGraph's initial-layout tracking matches the real image state.
        CommandBuffer cmd(m_rhi->ctx(), m_rhi->commandPool());
        cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VkClearColorValue clearValue{};
        clearValue.float32[0] = 0.0f;
        clearValue.float32[1] = 0.0f;
        clearValue.float32[2] = 0.0f;
        clearValue.float32[3] = 1.0f;
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        for (int i = 0; i < 2; ++i) {
            m_taaHistoryImages[i]->transitionLayout(cmd.handle(),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearColorImage(cmd.handle(), m_taaHistoryImages[i]->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
            m_taaHistoryImages[i]->transitionLayout(cmd.handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        cmd.end();
        cmd.submit(m_rhi->ctx().graphicsQueue());
        vkDeviceWaitIdle(m_rhi->ctx().device());

        m_taaHistoryReadIndex = 0;
        m_taaFrameIndex = 0;
        m_taaHistoryHandles[0] = m_renderGraph->addImportedTexture("TAAHistory0",
            {.width = extent.width,
             .height = extent.height,
             .format = VK_FORMAT_R16G16B16A16_SFLOAT,
             .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT},
            m_taaHistoryImages[0]->handle(), m_taaHistoryImages[0]->view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_taaHistoryHandles[1] = m_renderGraph->addImportedTexture("TAAHistory1",
            {.width = extent.width,
             .height = extent.height,
             .format = VK_FORMAT_R16G16B16A16_SFLOAT,
             .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT},
            m_taaHistoryImages[1]->handle(), m_taaHistoryImages[1]->view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_taaPass = std::make_unique<TAAPass>();
    m_taaPass->setInputHDR(m_ssrCompositePass->outputHandle());
    m_taaPass->setInputDepth(m_gbufferPass->depthHandle());
    m_taaPass->setHistoryRead(m_taaHistoryHandles[0]);
    m_taaPass->setHistoryWrite(m_taaHistoryHandles[1]);
    m_taaPass->declare(m_rhi, m_renderGraph.get());

    m_bloomPass = std::make_unique<BloomPass>();
    m_bloomPass->setInputHDR(m_taaPass->outputHandle());
    m_bloomPass->declare(m_rhi, m_renderGraph.get());

    m_tonemapPass = std::make_unique<TonemapPass>();
    // Tonemap reads the original HDR scene color (after TAA) and the bloom contribution.
    m_tonemapPass->setInput(m_taaPass->outputHandle());
    m_tonemapPass->setBloomInput(m_bloomPass->outputHandle());
    m_tonemapPass->declare(m_rhi, m_renderGraph.get());

    m_fxaaPass = std::make_unique<FXAAPass>();
    m_fxaaPass->setInput(m_tonemapPass->outputHandle());
    m_fxaaPass->declare(m_rhi, m_renderGraph.get());

    m_presentPass = std::make_unique<PresentPass>();
    m_presentPass->setInput(m_fxaaPass->outputHandle());
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
    m_ssaoPass->create(passCtx);
    m_ssaoBlurPass->create(passCtx);
    m_lightingPass->create(passCtx);
    m_lightVisualizePass->create(passCtx);
    m_hizPass->create(passCtx);
    m_ssrPass->create(passCtx);
    m_ssrBlurHPass->create(passCtx);
    m_ssrCompositePass->create(passCtx);
    m_taaPass->create(passCtx);
    m_bloomPass->create(passCtx);
    m_tonemapPass->create(passCtx);
    m_fxaaPass->create(passCtx);
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

    if (m_bloomPass) {
        m_bloomPass->setEnabled(m_lightingSettings.enableBloom);
        m_bloomPass->setThreshold(m_lightingSettings.bloomThreshold);
        m_bloomPass->setIntensity(m_lightingSettings.bloomIntensity);
    }

    if (m_tonemapPass) {
        m_tonemapPass->setExposure(m_lightingSettings.exposure);
        m_tonemapPass->setGamma(m_lightingSettings.gamma);
        m_tonemapPass->setMode(m_lightingSettings.toneMappingMode);
        m_tonemapPass->setBloomIntensity(m_lightingSettings.bloomIntensity);
    }

    if (m_fxaaPass) {
        m_fxaaPass->setEnabled(m_lightingSettings.enableFXAA);
    }

    if (m_ssrPass) {
        m_ssrPass->setEnabled(m_ssrEnabled);
        m_ssrPass->setDisplayMode(m_ssrDisplayMode);
        m_ssrPass->setTraceMode(m_ssrTraceMode);
        m_ssrPass->setMaxDistance(m_ssrMaxDistance);
        m_ssrPass->setStride(m_ssrStride);
        m_ssrPass->setThickness(m_ssrThickness);
        m_ssrPass->setStepCount(m_ssrStepCount);
        m_ssrPass->setBinarySearchSteps(m_ssrBinarySearchSteps);
        m_ssrPass->setJitterEnabled(m_ssrJitterEnabled);
        m_ssrPass->setHizVisMip(m_ssrHizVisMip);

        m_ssrBlurHPass->setRadius(m_ssrBlurRadius);
        m_ssrCompositePass->setRadius(m_ssrBlurRadius);
        m_ssrCompositePass->setDisplayMode(m_ssrDisplayMode);
    }

    // ---- CPU frame-time tracking per SSR mode ----
    {
        double now = glfwGetTime();
        if (m_lastFrameTime > 0.0) {
            float dtMs = static_cast<float>((now - m_lastFrameTime) * 1000.0);
            int modeIdx = m_ssrEnabled ? m_ssrTraceMode : 0;
            float& avg = m_avgFrameTimeMs[modeIdx];
            avg = avg * 0.95f + dtMs * 0.05f;
        }
        m_lastFrameTime = now;
    }

    // ---- TAA jitter / matrix setup ----
    bool taaEnabled = m_lightingSettings.enableTAA;
    glm::vec2 jitter(0.0f);
    if (taaEnabled) {
        jitter = computeTAAJitter(m_taaFrameIndex, m_rhi->extent());
    }
    m_camera->setJitter(jitter);

    float aspect = static_cast<float>(m_rhi->extent().width) / m_rhi->extent().height;
    glm::mat4 view = m_camera->getViewMatrix();
    glm::mat4 proj = m_camera->getJitteredProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * view;

    if (m_taaPass) {
        // On the first TAA frame (or after a resize) the history is just the
        // current frame, so prev matrices equal current matrices.
        if (m_taaFrameIndex == 0) {
            m_prevView = view;
            m_prevProj = proj;
            m_prevViewProj = viewProj;
        }

        m_taaPass->setEnabled(taaEnabled);
        m_taaPass->setMatrices(glm::inverse(viewProj), m_prevViewProj);

        // Ensure the RenderGraph knows which physical images the ping-pong
        // handles currently refer to.
        uint32_t readIdx = m_taaHistoryReadIndex;
        uint32_t writeIdx = 1 - readIdx;
        m_renderGraph->bindImportedTexture(
            m_taaHistoryHandles[readIdx],
            m_taaHistoryImages[readIdx]->handle(),
            m_taaHistoryImages[readIdx]->view());
        m_renderGraph->bindImportedTexture(
            m_taaHistoryHandles[writeIdx],
            m_taaHistoryImages[writeIdx]->handle(),
            m_taaHistoryImages[writeIdx]->view());
    }

    PassExecuteContext passCtx{};
    passCtx.cmd = frame.cmd;
    passCtx.imageIndex = frame.imageIndex;
    passCtx.camera = m_camera;
    passCtx.light = &m_scene->directionalLight();
    m_renderGraph->execute(passCtx);

    // ---- Prepare state for next frame ----
    m_prevView = view;
    m_prevProj = proj;
    m_prevViewProj = viewProj;
    if (taaEnabled) {
        m_taaHistoryReadIndex = 1 - m_taaHistoryReadIndex;
        ++m_taaFrameIndex;
    }
    // Reset jitter so non-TAA passes / ImGui use an unjittered projection.
    m_camera->setJitter(glm::vec2(0.0f));
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

    PanelItem ssaoItem{};
    ssaoItem.type = PanelItem::Bool;
    ssaoItem.label = "Enable SSAO";
    ssaoItem.b.value = &m_lightingSettings.enableSSAO;
    desc.items.push_back(ssaoItem);

    static const char* toneModes[] = {"Reinhard", "ACES"};
    PanelItem toneModeItem{};
    toneModeItem.type = PanelItem::Enum;
    toneModeItem.label = "Tone Mapping";
    toneModeItem.e.value = &m_lightingSettings.toneMappingMode;
    toneModeItem.e.names = toneModes;
    toneModeItem.e.count = 2;
    desc.items.push_back(toneModeItem);

    PanelItem exposureItem{};
    exposureItem.type = PanelItem::Float;
    exposureItem.label = "Exposure";
    exposureItem.f.value = &m_lightingSettings.exposure;
    exposureItem.f.min = 0.1f;
    exposureItem.f.max = 5.0f;
    desc.items.push_back(exposureItem);

    PanelItem gammaItem{};
    gammaItem.type = PanelItem::Float;
    gammaItem.label = "Gamma";
    gammaItem.f.value = &m_lightingSettings.gamma;
    gammaItem.f.min = 1.0f;
    gammaItem.f.max = 3.0f;
    desc.items.push_back(gammaItem);

    PanelItem bloomEnableItem{};
    bloomEnableItem.type = PanelItem::Bool;
    bloomEnableItem.label = "Enable Bloom";
    bloomEnableItem.b.value = &m_lightingSettings.enableBloom;
    desc.items.push_back(bloomEnableItem);

    PanelItem bloomThresholdItem{};
    bloomThresholdItem.type = PanelItem::Float;
    bloomThresholdItem.label = "Bloom Threshold";
    bloomThresholdItem.f.value = &m_lightingSettings.bloomThreshold;
    bloomThresholdItem.f.min = 0.0f;
    bloomThresholdItem.f.max = 5.0f;
    desc.items.push_back(bloomThresholdItem);

    PanelItem bloomIntensityItem{};
    bloomIntensityItem.type = PanelItem::Float;
    bloomIntensityItem.label = "Bloom Intensity";
    bloomIntensityItem.f.value = &m_lightingSettings.bloomIntensity;
    bloomIntensityItem.f.min = 0.0f;
    bloomIntensityItem.f.max = 2.0f;
    desc.items.push_back(bloomIntensityItem);

    PanelItem fxaaItem{};
    fxaaItem.type = PanelItem::Bool;
    fxaaItem.label = "Enable FXAA";
    fxaaItem.b.value = &m_lightingSettings.enableFXAA;
    desc.items.push_back(fxaaItem);

    desc.items.push_back({PanelItem::Separator, "SSR", {}});

    PanelItem ssrEnableItem{};
    ssrEnableItem.type = PanelItem::Bool;
    ssrEnableItem.label = "Enable SSR";
    ssrEnableItem.b.value = &m_ssrEnabled;
    desc.items.push_back(ssrEnableItem);

    static const char* ssrDebugModes[] = {"Composite", "Reflection Only", "Hit Mask", "Thickness", "Raw Depth", "HiZ Mip"};
    PanelItem ssrDebugItem{};
    ssrDebugItem.type = PanelItem::Enum;
    ssrDebugItem.label = "SSR Debug";
    ssrDebugItem.e.value = &m_ssrDisplayMode;
    ssrDebugItem.e.names = ssrDebugModes;
    ssrDebugItem.e.count = 6;
    desc.items.push_back(ssrDebugItem);

    PanelItem ssrMaxDistanceItem{};
    ssrMaxDistanceItem.type = PanelItem::Float;
    ssrMaxDistanceItem.label = "SSR Max Distance";
    ssrMaxDistanceItem.f.value = &m_ssrMaxDistance;
    ssrMaxDistanceItem.f.min = 0.5f;
    ssrMaxDistanceItem.f.max = 30.0f;
    desc.items.push_back(ssrMaxDistanceItem);

    PanelItem ssrStrideItem{};
    ssrStrideItem.type = PanelItem::Float;
    ssrStrideItem.label = "SSR Stride";
    ssrStrideItem.f.value = &m_ssrStride;
    ssrStrideItem.f.min = 1.0f;
    ssrStrideItem.f.max = 64.0f;
    desc.items.push_back(ssrStrideItem);

    PanelItem ssrStepCountItem{};
    ssrStepCountItem.type = PanelItem::Int;
    ssrStepCountItem.label = "SSR Step Count";
    ssrStepCountItem.i.value = &m_ssrStepCount;
    ssrStepCountItem.i.min = 1;
    ssrStepCountItem.i.max = 512;
    desc.items.push_back(ssrStepCountItem);

    PanelItem ssrThicknessItem{};
    ssrThicknessItem.type = PanelItem::Float;
    ssrThicknessItem.label = "SSR Thickness";
    ssrThicknessItem.f.value = &m_ssrThickness;
    ssrThicknessItem.f.min = 0.0f;
    ssrThicknessItem.f.max = 0.1f;
    desc.items.push_back(ssrThicknessItem);

    static const char* ssrTraceModes[] = {"Basic", "DDA", "Hi-Z"};
    PanelItem ssrTraceItem{};
    ssrTraceItem.type = PanelItem::Enum;
    ssrTraceItem.label = "SSR Trace Mode";
    ssrTraceItem.e.value = &m_ssrTraceMode;
    ssrTraceItem.e.names = ssrTraceModes;
    ssrTraceItem.e.count = 3;
    desc.items.push_back(ssrTraceItem);

    PanelItem ssrBinaryStepsItem{};
    ssrBinaryStepsItem.type = PanelItem::Int;
    ssrBinaryStepsItem.label = "SSR Binary Steps";
    ssrBinaryStepsItem.i.value = &m_ssrBinarySearchSteps;
    ssrBinaryStepsItem.i.min = 1;
    ssrBinaryStepsItem.i.max = 16;
    desc.items.push_back(ssrBinaryStepsItem);

    PanelItem ssrJitterItem{};
    ssrJitterItem.type = PanelItem::Bool;
    ssrJitterItem.label = "SSR Jitter";
    ssrJitterItem.b.value = &m_ssrJitterEnabled;
    desc.items.push_back(ssrJitterItem);

    PanelItem ssrBlurRadiusItem{};
    ssrBlurRadiusItem.type = PanelItem::Float;
    ssrBlurRadiusItem.label = "SSR Blur Radius";
    ssrBlurRadiusItem.f.value = &m_ssrBlurRadius;
    ssrBlurRadiusItem.f.min = 0.0f;
    ssrBlurRadiusItem.f.max = 8.0f;
    desc.items.push_back(ssrBlurRadiusItem);

    PanelItem ssrHizMipItem{};
    ssrHizMipItem.type = PanelItem::Int;
    ssrHizMipItem.label = "HiZ Vis Mip";
    ssrHizMipItem.i.value = &m_ssrHizVisMip;
    ssrHizMipItem.i.min = 0;
    ssrHizMipItem.i.max = 16;
    desc.items.push_back(ssrHizMipItem);

    if (m_ssrPass) {
        char buf[128];
        PanelItem gpuLabel{};
        gpuLabel.type = PanelItem::Label;
        std::snprintf(buf, sizeof(buf),
            "SSR GPU: B=%.3f/%.3f  D=%.3f/%.3f  H=%.3f/%.3f ms",
            m_ssrPass->lastGpuTimeMs(0), m_ssrPass->avgGpuTimeMs(0),
            m_ssrPass->lastGpuTimeMs(1), m_ssrPass->avgGpuTimeMs(1),
            m_ssrPass->lastGpuTimeMs(2), m_ssrPass->avgGpuTimeMs(2));
        gpuLabel.label = buf;
        desc.items.push_back(gpuLabel);

        PanelItem stepLabel{};
        stepLabel.type = PanelItem::Label;
        std::snprintf(buf, sizeof(buf),
            "Avg ray steps: B=%.1f  D=%.1f  H=%.1f",
            m_ssrPass->avgSteps(0), m_ssrPass->avgSteps(1), m_ssrPass->avgSteps(2));
        stepLabel.label = buf;
        desc.items.push_back(stepLabel);

        int modeIdx = m_ssrEnabled ? m_ssrTraceMode : 0;
        PanelItem frameLabel{};
        frameLabel.type = PanelItem::Label;
        std::snprintf(buf, sizeof(buf),
            "Frame time (%s): %.3f ms",
            ssrTraceModes[modeIdx], m_avgFrameTimeMs[modeIdx]);
        frameLabel.label = buf;
        desc.items.push_back(frameLabel);
    }

    PanelItem taaItem{};
    taaItem.type = PanelItem::Bool;
    taaItem.label = "Enable TAA";
    taaItem.b.value = &m_lightingSettings.enableTAA;
    desc.items.push_back(taaItem);

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
