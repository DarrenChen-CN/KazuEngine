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

    m_gbufferPass = std::make_unique<GBufferPass>();
    m_gbufferPass->declare(m_rhi, m_renderGraph.get());

    m_lightingPass = std::make_unique<LightingPass>();
    m_lightingPass->setInputs(
        m_gbufferPass->albedoHandle(),
        m_gbufferPass->normalHandle());
    m_lightingPass->declare(m_rhi, m_renderGraph.get());

    // ---- Phase 2: Compile ----
    if (!m_renderGraph->compile()) {
        fatalError("RenderGraph compile failed");
    }

    // ---- Phase 3: Create VK objects ----
    m_gbufferPass->create(m_scene, m_camera, m_renderGraph.get());
    m_lightingPass->create(m_scene, m_camera, m_renderGraph.get());

    spdlog::info("DeferredShading initialized (pure composer)");
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
    if (m_lightingPass) m_lightingPass->setCurrentImageIndex(idx);
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
