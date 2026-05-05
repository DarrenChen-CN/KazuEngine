// ============================================================================
// KazuEngine - Core Layer: RenderPass (Implementation)
// ============================================================================

#include "RenderPass.h"
#include "Utils.h"

namespace kazu {

RenderPass::RenderPass(Context& ctx, const VkRenderPassCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreateRenderPass(m_ctx->device(), &createInfo, nullptr, &m_renderPass));
}

RenderPass::~RenderPass() {
    if (m_renderPass != VK_NULL_HANDLE && m_ctx) {
        vkDestroyRenderPass(m_ctx->device(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

RenderPass::RenderPass(RenderPass&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_renderPass(other.m_renderPass)
{
    other.m_renderPass = VK_NULL_HANDLE;
}

RenderPass& RenderPass::operator=(RenderPass&& other) noexcept {
    if (this != &other) {
        this->~RenderPass();
        m_ctx = other.m_ctx;
        m_renderPass = other.m_renderPass;
        other.m_renderPass = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
