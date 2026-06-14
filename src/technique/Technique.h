// ============================================================================
// KazuEngine - Technique Layer: Base Interface
//
// A Technique is a semantic render pipeline composition, such as Deferred
// Shading or a future Forward+/Path Trace preview. It owns/assembles passes
// and hides its RenderGraph details from Application.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include "app/AppUI.h"

namespace kazu {

class RHI;
class Scene;
class Camera;

class Technique {
public:
    virtual ~Technique() = default;

    virtual const char* name() const = 0;
    void init(RHI* rhi, Scene* scene, Camera* camera) {
        m_rhi = rhi;
        m_scene = scene;
        m_camera = camera;
        onInit();
    }

    virtual void setCurrentImageIndex(uint32_t imageIndex) = 0;
    virtual void bindSwapchainImage(uint32_t imageIndex) = 0;
    virtual void render(VkCommandBuffer cmd) = 0;

    virtual void exposePanel(PanelDesc& desc) = 0;
    virtual bool onKey(int key, int scancode, int action, int mods) {
        (void)key;
        (void)scancode;
        (void)action;
        (void)mods;
        return false;
    }

protected:
    virtual void onInit() = 0;

    RHI* m_rhi = nullptr;
    Scene* m_scene = nullptr;
    Camera* m_camera = nullptr;
};

} // namespace kazu
