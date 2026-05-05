// ============================================================================
// KazuEngine - Core Layer: DescriptorSetLayout (Implementation)
// ============================================================================

#include "DescriptorSetLayout.h"
#include "Utils.h"

namespace kazu {

DescriptorSetLayout::DescriptorSetLayout(Context& ctx, const VkDescriptorSetLayoutCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreateDescriptorSetLayout(m_ctx->device(), &createInfo, nullptr, &m_layout));
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (m_layout != VK_NULL_HANDLE && m_ctx) {
        vkDestroyDescriptorSetLayout(m_ctx->device(), m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_layout(other.m_layout)
{
    other.m_layout = VK_NULL_HANDLE;
}

DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept {
    if (this != &other) {
        this->~DescriptorSetLayout();
        m_ctx = other.m_ctx;
        m_layout = other.m_layout;
        other.m_layout = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
