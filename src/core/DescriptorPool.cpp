// ============================================================================
// KazuEngine - Core Layer: DescriptorPool (Implementation)
// ============================================================================

#include "DescriptorPool.h"
#include "Utils.h"

namespace kazu {

DescriptorPool::DescriptorPool(Context& ctx, const VkDescriptorPoolCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreateDescriptorPool(m_ctx->device(), &createInfo, nullptr, &m_pool));
}

DescriptorPool::~DescriptorPool() {
    if (m_pool != VK_NULL_HANDLE && m_ctx) {
        vkDestroyDescriptorPool(m_ctx->device(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
}

DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_pool(other.m_pool)
{
    other.m_pool = VK_NULL_HANDLE;
}

DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other) noexcept {
    if (this != &other) {
        this->~DescriptorPool();
        m_ctx = other.m_ctx;
        m_pool = other.m_pool;
        other.m_pool = VK_NULL_HANDLE;
    }
    return *this;
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_ctx->device(), &allocInfo, &set));
    return set;
}

void DescriptorPool::free(VkDescriptorSet set) {
    vkFreeDescriptorSets(m_ctx->device(), m_pool, 1, &set);
}

} // namespace kazu
