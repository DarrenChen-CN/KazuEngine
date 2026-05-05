// ============================================================================
// KazuEngine - Core Layer: RenderPass
//
// RAII wrapper for VkRenderPass.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class RenderPass {
public:
    RenderPass(Context& ctx, const VkRenderPassCreateInfo& createInfo);
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&& other) noexcept;
    RenderPass& operator=(RenderPass&& other) noexcept;

    VkRenderPass handle() const { return m_renderPass; }

private:
    Context* m_ctx = nullptr;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

} // namespace kazu
