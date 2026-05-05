// ============================================================================
// KazuEngine - Core Layer: PipelineLayout (Implementation)
// ============================================================================

#include "PipelineLayout.h"
#include "Utils.h"

namespace kazu {

PipelineLayout::PipelineLayout(Context& ctx, const VkPipelineLayoutCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreatePipelineLayout(m_ctx->device(), &createInfo, nullptr, &m_layout));
}

PipelineLayout::~PipelineLayout() {
    if (m_layout != VK_NULL_HANDLE && m_ctx) {
        vkDestroyPipelineLayout(m_ctx->device(), m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_layout(other.m_layout)
{
    other.m_layout = VK_NULL_HANDLE;
}

PipelineLayout& PipelineLayout::operator=(PipelineLayout&& other) noexcept {
    if (this != &other) {
        this->~PipelineLayout();
        m_ctx = other.m_ctx;
        m_layout = other.m_layout;
        other.m_layout = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
