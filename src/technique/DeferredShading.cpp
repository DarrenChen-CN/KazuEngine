// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Pure Composer)
// ============================================================================

#include "technique/DeferredShading.h"
#include "pass/GBufferPass.h"
#include "pass/LightingPass.h"
#include "app/AppUI.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"

namespace kazu {

DeferredShading::DeferredShading() = default;

DeferredShading::~DeferredShading() {
    if (!m_rhi) return;
    vkDeviceWaitIdle(m_rhi->ctx().device());
}

void DeferredShading::init(RHI* rhi, Scene* scene, Camera* camera) {
    m_rhi = rhi;
    m_scene = scene;
    m_camera = camera;

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

    m_gbufferPass = std::make_unique<GBufferPass>();
    m_gbufferPass->declare(m_rhi, m_renderGraph.get());

    m_lightingPass = std::make_unique<LightingPass>();
    m_lightingPass->setInputs(
        m_gbufferPass->albedoHandle(),
        m_gbufferPass->normalHandle(),
        m_gbufferPass->depthHandle());
    m_lightingPass->setSwapchainHandle(m_swapchainHandle);
    m_lightingPass->declare(m_rhi, m_renderGraph.get());

    // ---- Phase 2: Compile ----
    if (!m_renderGraph->compile()) {
        fatalError("RenderGraph compile failed");
    }

    // ---- Phase 3: Create VK objects ----
    m_gbufferPass->create(m_scene, m_camera, m_renderGraph.get());

    // Build scene materials now that ShaderEffect is ready
    m_scene->buildMaterials(m_rhi->ctx(), m_gbufferPass->shaderEffect(),
                            m_rhi->dslCache());

    m_lightingPass->create(m_scene, m_camera, m_renderGraph.get());

    // Restore display mode after resize re-init
    m_lightingPass->setDisplayMode(m_displayMode);

    spdlog::info("DeferredShading initialized (pure composer)");
}

void DeferredShading::bindSwapchainImage(uint32_t imageIndex) {
    if (m_renderGraph && m_swapchainHandle != RenderGraph::InvalidResource) {
        m_renderGraph->bindImportedTexture(
            m_swapchainHandle,
            m_rhi->swapchainImage(imageIndex),
            m_rhi->swapchainImageView(imageIndex));
    }
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

void DeferredShading::setCurrentImageIndex(uint32_t idx) {
    m_currentImageIndex = idx;
    if (m_lightingPass) {
        m_lightingPass->setCurrentImageIndex(idx);
        m_lightingPass->setDisplayMode(m_displayMode);
    }
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
