// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
// ============================================================================

#include "technique/DeferredShading.h"
#include "pass/GBufferPass.h"
#include "pass/LightingPass.h"
#include "pass/PresentPass.h"
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
    // ---- Phase 1: Declare ----
    m_renderGraph = std::make_unique<RenderGraph>(m_rhi->ctx());

    // Import swapchain as an external resource (handle is rebound per-frame)
    m_swapchainHandle = m_renderGraph->addImportedTexture(
        "Swapchain",
        {m_rhi->extent().width, m_rhi->extent().height,
         m_rhi->swapchainFormat(),
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
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

    m_lightingPass = std::make_unique<LightingPass>();
    m_lightingPass->setInputs(
        m_gbufferPass->albedoHandle(),
        m_gbufferPass->normalHandle(),
        m_gbufferPass->depthHandle());
    m_lightingPass->declare(m_rhi, m_renderGraph.get());

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
    passCtx.camera = m_camera;

    m_gbufferPass->create(passCtx);

    // Build scene materials now that ShaderEffect is ready
    m_scene->buildMaterials(m_rhi->ctx(), m_gbufferPass->shaderEffect(),
                            m_rhi->dslCache());

    m_lightingPass->create(passCtx);
    m_presentPass->create(passCtx);

    // Restore display mode after resize re-init
    m_lightingPass->setDisplayMode(m_displayMode);

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
        m_lightingPass->setDisplayMode(m_displayMode);
    }

    m_renderGraph->execute(frame.cmd, frame.imageIndex);
}

void DeferredShading::exposePanel(PanelDesc& desc) {
    desc.name = "Deferred Shading";
    static const char* modes[] = {"Lighting", "Albedo", "Normal"};
    PanelItem displayModeItem{};
    displayModeItem.type = PanelItem::Enum;
    displayModeItem.label = "Display Mode";
    displayModeItem.e.value = &m_displayMode;
    displayModeItem.e.names = modes;
    displayModeItem.e.count = 3;
    desc.items.push_back(displayModeItem);
    desc.items.push_back({PanelItem::Separator, "", {}});
}

void DeferredShading::setDisplayMode(int mode) {
    m_displayMode = mode;
    if (m_lightingPass) m_lightingPass->setDisplayMode(mode);
}

bool DeferredShading::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key != GLFW_KEY_D || action != GLFW_PRESS) return false;

    int mode = (displayMode() + 1) % 3;
    setDisplayMode(mode);
    const char* modeName = (mode == 0) ? "lighting" : (mode == 1) ? "albedo" : "normal";
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
